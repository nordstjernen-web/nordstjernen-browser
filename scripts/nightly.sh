#!/usr/bin/env bash
# Nordstjernen nightly build orchestrator. Builds, from a single Linux host,
# a source tarball, per-distro Linux packages (debian/ubuntu/opensuse/alpine
# via containers, in parallel), and Windows, macOS and BSD (FreeBSD/NetBSD)
# builds (by driving the GitHub Actions runners, dispatched up front so
# they run while the local container builds proceed), then collects everything
# into $NIGHTLY_ROOT with
# checksums, a manifest, and stable download symlinks. Intended to run
# unattended from cron. See --help.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

NIGHTLY_ROOT=${NIGHTLY_ROOT:-/var/www/html/nightly}
NIGHTLY_REF=${NIGHTLY_REF:-origin/main}
NIGHTLY_GHA_BRANCH=${NIGHTLY_GHA_BRANCH:-main}
NIGHTLY_GHA_TIMEOUT=${NIGHTLY_GHA_TIMEOUT:-4200}
NIGHTLY_GHA_DISPATCH=${NIGHTLY_GHA_DISPATCH:-1}
NIGHTLY_PULL=${NIGHTLY_PULL:-1}
NIGHTLY_PULL_BRANCH=${NIGHTLY_PULL_BRANCH:-main}
DOCKER=${NS_DOCKER:-docker}

# Run the four distro container builds concurrently (1) or one at a time (0).
# When concurrent, the per-container compile job count is bounded so the host
# is not oversubscribed (see run_distro_builds).
NIGHTLY_PARALLEL=${NIGHTLY_PARALLEL:-1}
# Retry counts for flaky network operations.
NIGHTLY_DOCKER_PULL_RETRIES=${NIGHTLY_DOCKER_PULL_RETRIES:-3}
NIGHTLY_GH_RETRIES=${NIGHTLY_GH_RETRIES:-4}

NIGHTLY_DEBIAN_IMAGE=${NIGHTLY_DEBIAN_IMAGE:-debian:trixie}
NIGHTLY_UBUNTU_IMAGE=${NIGHTLY_UBUNTU_IMAGE:-ubuntu:26.04}
NIGHTLY_OPENSUSE_IMAGE=${NIGHTLY_OPENSUSE_IMAGE:-opensuse/tumbleweed}
NIGHTLY_ALPINE_IMAGE=${NIGHTLY_ALPINE_IMAGE:-alpine:edge}

DO_TARBALL=1
DO_DOCKER=1
DO_GHA=1
DO_JAVA=1
DATE=""

usage() {
    cat <<EOF
Usage: ./scripts/nightly.sh [options]

Builds nightly artifacts for source, Linux (debian/ubuntu/opensuse/alpine via
containers, in parallel), Windows and macOS (via GitHub Actions) into
\$NIGHTLY_ROOT.

Options:
  --date YYYY-MM-DD   Output dir date stamp (default: today, UTC).
  --root DIR          Output root (default: \$NIGHTLY_ROOT or /var/www/html/nightly).
  --ref REF           Git ref to archive/build (default: origin/main).
  --no-tarball        Skip the source tarball stage.
  --no-docker         Skip the Linux container builds.
  --no-gha            Skip driving Windows/macOS via GitHub Actions.
  --no-java           Skip the Java API jar/javadoc stage.
  --no-parallel       Build the Linux containers one at a time.
  --no-pull           Don't fast-forward the working tree to origin/main first.
  -h, --help          Show this help.

The Windows/macOS GitHub Actions runs are dispatched before the local
container builds start, so the remote builds proceed while the containers
compile; their artifacts are collected afterwards.

Before building, the script fast-forwards its own checkout to
origin/\$NIGHTLY_PULL_BRANCH (default main) and re-runs itself if that
moved nightly.sh, so a plain cron invocation always builds the latest
orchestrator. A dirty or diverged working tree is left untouched.

Environment overrides: NIGHTLY_ROOT, NIGHTLY_REF, NIGHTLY_PULL,
NIGHTLY_PULL_BRANCH, NIGHTLY_PARALLEL, NIGHTLY_GHA_TIMEOUT,
NIGHTLY_GHA_BRANCH, NIGHTLY_GHA_DISPATCH, NIGHTLY_DOCKER_PULL_RETRIES,
NIGHTLY_GH_RETRIES, NS_DOCKER, the
NIGHTLY_{DEBIAN,UBUNTU,OPENSUSE,ALPINE}_IMAGE image tags, and the WebGPU
knobs NS_WEBGPU (0/1/auto) and WGPU_NATIVE_VERSION forwarded to the
container builds.
EOF
}

ORIG_ARGS=("$@")

while [ $# -gt 0 ]; do
    case "$1" in
        --date)       DATE="$2"; shift 2 ;;
        --root)       NIGHTLY_ROOT="$2"; shift 2 ;;
        --ref)        NIGHTLY_REF="$2"; shift 2 ;;
        --no-tarball) DO_TARBALL=0; shift ;;
        --no-docker)  DO_DOCKER=0; shift ;;
        --no-gha)     DO_GHA=0; shift ;;
        --no-java)    DO_JAVA=0; shift ;;
        --no-parallel) NIGHTLY_PARALLEL=0; shift ;;
        --no-pull)    NIGHTLY_PULL=0; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 2 ;;
    esac
done

cd "$ROOT"

if [ "$NIGHTLY_PULL" = 1 ] && [ "${NIGHTLY_SELF_UPDATED:-0}" != 1 ]; then
    before=$(git rev-parse HEAD 2>/dev/null || echo none)
    if git fetch --quiet origin "$NIGHTLY_PULL_BRANCH"; then
        if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
            echo "warn: working tree not clean; skipping pull, using it as-is"
        elif ! git merge --ff-only --quiet "origin/$NIGHTLY_PULL_BRANCH"; then
            echo "warn: cannot fast-forward to origin/$NIGHTLY_PULL_BRANCH (diverged?); using working tree as-is"
        fi
    else
        echo "warn: git fetch failed; using local refs"
    fi
    after=$(git rev-parse HEAD 2>/dev/null || echo none)
    if [ "$before" != "$after" ]; then
        echo "orchestrator updated ${before:0:7}..${after:0:7}; re-running latest nightly.sh"
        export NIGHTLY_SELF_UPDATED=1
        exec bash "$0" ${ORIG_ARGS[@]+"${ORIG_ARGS[@]}"}
    fi
else
    git fetch --quiet origin || echo "warn: git fetch failed; using local refs"
fi

[ -n "$DATE" ] || DATE=$(date -u +%Y-%m-%d)
DATESTAMP=${DATE//-/}
COMMIT=$(git rev-parse --short "$NIGHTLY_REF" 2>/dev/null || git rev-parse --short HEAD)
MESON_VERSION=$(grep -E "^[[:space:]]*version" "$ROOT/meson.build" | head -1 \
                | sed -E "s/.*version: '([^']+)'.*/\1/")
NVERSION="${MESON_VERSION}+nightly.${DATESTAMP}.g${COMMIT}"

OUTDIR="$NIGHTLY_ROOT"
WORK=$(mktemp -d)
STATUSDIR="$WORK/status"
mkdir -p "$OUTDIR" "$STATUSDIR"
rm -rf "$OUTDIR/source" "$OUTDIR/linux" "$OUTDIR/windows" "$OUTDIR/macos" "$OUTDIR/java" \
       "$OUTDIR/freebsd" "$OUTDIR/netbsd"
rm -f "$OUTDIR"/SHA256SUMS "$OUTDIR"/MANIFEST.txt "$OUTDIR"/nightly.log \
      "$OUTDIR"/nordstjernen-*
LOG="$OUTDIR/nightly.log"
trap 'rm -rf "$WORK"' EXIT

exec > >(tee -a "$LOG") 2>&1

# Stage results are recorded as files under $STATUSDIR rather than an
# in-memory array so that background (parallel) stages report their outcome
# back to this process. Each file holds a tab-separated "<state>\t<message>".
set_status() { printf '%s\t%s\n' "$2" "${3:-}" > "$STATUSDIR/$1"; }

log()  { printf '\n=== %s ===\n' "$*"; }
ok()   { set_status "$1" ok;      printf '[ ok ]   %s\n' "$1"; }
fail() { set_status "$1" FAILED "${2:-}"; printf '[FAIL]   %s — %s\n' "$1" "${2:-}"; }
skip() { set_status "$1" skipped; printf '[skip]   %s\n' "$1"; }
dump_tail() {
    local f="$1" n="${2:-80}"
    if [ ! -s "$f" ]; then
        printf -- '----- %s is empty or missing -----\n' "$f"
        return 0
    fi
    printf -- '----- %s (last %s lines) -----\n' "$f" "$n"
    tail -n "$n" "$f"
    printf -- '----- end %s -----\n' "$f"
}

# Run a command, retrying with exponential backoff (2s, 4s, 8s, ...) on
# failure. Usage: retry <attempts> <cmd> [args...].
retry() {
    local attempts="$1"; shift
    local i=1 delay=2
    until "$@"; do
        if [ "$i" -ge "$attempts" ]; then
            return 1
        fi
        echo "retry: '$1' failed (attempt $i/$attempts); waiting ${delay}s..." >&2
        sleep "$delay"
        delay=$((delay * 2))
        i=$((i + 1))
    done
}

docker_pull() { retry "$NIGHTLY_DOCKER_PULL_RETRIES" "$DOCKER" pull "$1"; }

log "Nordstjernen nightly $DATE"
printf 'ref=%s commit=%s version=%s\nroot=%s\n' \
    "$NIGHTLY_REF" "$COMMIT" "$NVERSION" "$OUTDIR"

archive_to() {
    git archive --format=tar --prefix="nordstjernen-${NVERSION}/" "$NIGHTLY_REF" \
        | tar -x -C "$1"
}

stage_tarball() {
    log "Stage: source tarball"
    local dst="$OUTDIR/source"
    mkdir -p "$dst"
    local base="nordstjernen-${NVERSION}"
    if git archive --format=tar --prefix="${base}/" "$NIGHTLY_REF" \
           | gzip -9 > "$dst/${base}.tar.gz" \
       && git archive --format=tar --prefix="${base}/" "$NIGHTLY_REF" \
           | xz -9 > "$dst/${base}.tar.xz"; then
        ok "source-tarball"
    else
        fail "source-tarball" "git archive failed"
    fi
}

stage_distro() {
    local distro="$1" image="$2" version="$3"
    local key="linux-$distro"
    log "Stage: $distro container build ($image)"
    if ! $DOCKER image inspect "$image" >/dev/null 2>&1; then
        docker_pull "$image" || { fail "$key" "pull $image failed"; return; }
    fi
    local src="$WORK/$distro"
    mkdir -p "$src"
    archive_to "$src"
    local tree="$src/nordstjernen-${NVERSION}"
    local dst="$OUTDIR/linux/$distro"
    mkdir -p "$dst"
    local -a dargs=( --rm -v "$tree:/build:z" -w /build
        -e "VERSION=$version" -e "NS_BUILD_DATE=$DATE" )
    [ -n "${DISTRO_JOBS:-}" ] && dargs+=( -e "NS_BUILD_JOBS=$DISTRO_JOBS" )
    # WebGPU: pack-linux.sh fetches wgpu-native inside the container and builds
    # it in (auto). Forward the operator's overrides so the whole nightly can
    # force it on/off (NS_WEBGPU) or pin a different wgpu-native release.
    [ -n "${NS_WEBGPU:-}" ] && dargs+=( -e "NS_WEBGPU=$NS_WEBGPU" )
    [ -n "${WGPU_NATIVE_VERSION:-}" ] && dargs+=( -e "WGPU_NATIVE_VERSION=$WGPU_NATIVE_VERSION" )
    if $DOCKER run "${dargs[@]}" "$image" \
        sh -c 'command -v bash >/dev/null 2>&1 || apk add --no-cache bash >/dev/null 2>&1 || true; exec bash scripts/nightly-distro-build.sh "$1"' sh "$distro" 2>&1 | tee "$dst/build.log"; then
        local n=0
        shopt -s nullglob
        for f in "$tree"/dist/*.zip "$tree"/dist/*.deb "$tree"/dist/*.rpm "$tree"/dist/*.apk; do
            cp "$f" "$dst/" && n=$((n+1))
        done
        shopt -u nullglob
        if [ "$n" -gt 0 ]; then ok "$key"; else fail "$key" "no artifacts produced (see $dst/build.log)"; fi
    else
        fail "$key" "container build failed (see $dst/build.log)"
    fi
    rm -rf "$src"
}

run_distro_builds() {
    local specs=(
        "debian:$NIGHTLY_DEBIAN_IMAGE"
        "ubuntu:$NIGHTLY_UBUNTU_IMAGE"
        "opensuse:$NIGHTLY_OPENSUSE_IMAGE"
        "alpine:$NIGHTLY_ALPINE_IMAGE"
    )
    local spec distro image
    if [ "$NIGHTLY_PARALLEL" != 1 ]; then
        DISTRO_JOBS=""
        for spec in "${specs[@]}"; do
            distro=${spec%%:*}; image=${spec#*:}
            stage_distro "$distro" "$image" "$NVERSION"
        done
        return
    fi

    # Bound total concurrency: split a memory-derived job budget (the same
    # heuristic each container would otherwise pick alone) across the distros
    # building at once, so four parallel compiles don't oversubscribe the host.
    local cores mem_gb budget
    cores=$(nproc 2>/dev/null || echo 2)
    mem_gb=$(awk '/MemTotal/{print int($2/1024/1024)}' /proc/meminfo 2>/dev/null || echo 4)
    budget=$(( mem_gb / 2 ))
    [ "$budget" -lt 1 ] && budget=1
    [ "$budget" -gt "$cores" ] && budget=$cores
    DISTRO_JOBS=$(( budget / ${#specs[@]} ))
    [ "$DISTRO_JOBS" -lt 2 ] && DISTRO_JOBS=2
    log "Parallel container builds: ${#specs[@]} distros @ -j${DISTRO_JOBS} each (host ${cores} cores / ${mem_gb} GiB)"

    # Stream each container's output live, tagged with its distro, instead of
    # buffering it until every build finishes, so the console shows progress
    # as it happens. The untagged full log is still saved per distro by
    # stage_distro (linux/<distro>/build.log).
    local start; start=$(date +%s)
    local -a pairs=()
    for spec in "${specs[@]}"; do
        distro=${spec%%:*}; image=${spec#*:}
        printf '[nightly] launching %-9s build (%s, -j%s)\n' "$distro" "$image" "$DISTRO_JOBS"
        ( stage_distro "$distro" "$image" "$NVERSION" 2>&1 \
            | awk -v t="$distro" '{ printf "[%s] %s\n", t, $0; fflush() }' ) &
        pairs+=("$!:$distro")
    done

    # Heartbeat: every 30s, name the builds still running and the elapsed time,
    # so a quiet compile phase doesn't look like a hang.
    local hb_pid
    (
        while :; do
            sleep 30
            local now running=() pn p n
            now=$(( ($(date +%s) - start) / 60 ))
            for pn in "${pairs[@]}"; do
                p=${pn%%:*}; n=${pn#*:}
                kill -0 "$p" 2>/dev/null && running+=("$n")
            done
            [ "${#running[@]}" -eq 0 ] && break
            printf '[nightly] +%dm still building: %s\n' "$now" "${running[*]}"
        done
    ) &
    hb_pid=$!

    local pn
    for pn in "${pairs[@]}"; do
        wait "${pn%%:*}" 2>/dev/null
    done
    kill "$hb_pid" 2>/dev/null
    wait "$hb_pid" 2>/dev/null
    printf '[nightly] all container builds finished in %dm\n' \
        "$(( ($(date +%s) - start) / 60 ))"
}

gha_dispatch() {
    local wf="$1" plat="$2"
    local key="gha-$plat"
    local idfile="$WORK/gha-$plat.id"
    echo 0 > "$idfile"
    log "Stage: GitHub Actions $plat ($wf) — dispatch"
    if ! gh auth status >/dev/null 2>&1; then
        fail "$key" "gh not authenticated"
        return
    fi
    local sha
    sha=$(git rev-parse "$NIGHTLY_REF" 2>/dev/null || git rev-parse HEAD)
    local rid rstatus rconc usable=0
    read -r rid rstatus rconc < <(gh api \
        "repos/{owner}/{repo}/actions/workflows/$wf/runs?head_sha=$sha&per_page=1" \
        --jq '.workflow_runs[0] | "\(.id // 0) \(.status // "none") \(.conclusion // "none")"' \
        2>/dev/null || echo "0 none none")
    if [ "${rid:-0}" != 0 ] && { [ "$rstatus" != completed ] || [ "$rconc" = success ]; }; then
        usable=1
        printf 'reusing %s run %s for %s (status=%s)\n' "$wf" "$rid" "${sha:0:7}" "$rstatus"
    fi
    if [ "$usable" != 1 ]; then
        if [ "$NIGHTLY_GHA_DISPATCH" != 1 ]; then
            fail "$key" "no usable $wf run for ${sha:0:7} (dispatch disabled)"
            return
        fi
        printf 'no usable %s run for %s; dispatching on %s\n' \
            "$wf" "${sha:0:7}" "$NIGHTLY_GHA_BRANCH"
        if ! retry "$NIGHTLY_GH_RETRIES" gh workflow run "$wf" --ref "$NIGHTLY_GHA_BRANCH"; then
            fail "$key" "workflow_dispatch failed"
            return
        fi
        rid=0
        local i
        for i in $(seq 1 40); do
            sleep 6
            rid=$(gh api \
                "repos/{owner}/{repo}/actions/workflows/$wf/runs?branch=$NIGHTLY_GHA_BRANCH&event=workflow_dispatch&per_page=1" \
                --jq '.workflow_runs[0].id // 0' 2>/dev/null || echo 0)
            [ "$rid" != 0 ] && break
        done
        if [ "$rid" = 0 ]; then
            fail "$key" "dispatched run did not appear (Actions disabled for this repo?)"
            return
        fi
    fi
    echo "$rid" > "$idfile"
    printf 'tracking %s run %s for %s\n' "$wf" "$rid" "$plat"
}

gha_collect() {
    local plat="$1"
    local key="gha-$plat"
    local rid
    rid=$(cat "$WORK/gha-$plat.id" 2>/dev/null || echo 0)
    if [ "${rid:-0}" = 0 ]; then
        return
    fi
    log "Stage: GitHub Actions $plat — collect run $rid"
    printf 'watching run %s (timeout %ss)\n' "$rid" "$NIGHTLY_GHA_TIMEOUT"
    local deadline=$(( $(date +%s) + NIGHTLY_GHA_TIMEOUT ))
    local status conclusion
    while :; do
        status=$(gh run view "$rid" --json status --jq '.status' 2>/dev/null || echo unknown)
        [ "$status" = completed ] && break
        if [ "$(date +%s)" -ge "$deadline" ]; then
            fail "$key" "timed out waiting for run $rid"
            return
        fi
        sleep 20
    done
    conclusion=$(gh run view "$rid" --json conclusion --jq '.conclusion' 2>/dev/null || echo unknown)
    local dst="$OUTDIR/$plat"
    mkdir -p "$dst"
    retry "$NIGHTLY_GH_RETRIES" gh run download "$rid" -D "$dst" 2>/dev/null || true
    if [ "$conclusion" = success ] && [ -n "$(find "$dst" -type f 2>/dev/null)" ]; then
        ok "$key"
    else
        fail "$key" "conclusion=$conclusion (artifacts may be partial)"
    fi
}

stage_java() {
    log "Stage: Java (runnable fat jar + sources + javadoc)"
    local key="java"
    local jhome="${JAVA_HOME:-}"
    if [ -z "$jhome" ] && command -v javac >/dev/null 2>&1; then
        jhome=$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")
    fi
    if [ -z "$jhome" ] || [ ! -x "$jhome/bin/javac" ]; then
        fail "$key" "JDK not found (JAVA_HOME='${JAVA_HOME:-}', no usable javac on PATH); install openjdk-21-jdk or set JAVA_HOME"
        return
    fi
    local dst="$OUTDIR/java"
    mkdir -p "$dst"
    local blog="$dst/build.log"
    local work="$WORK/javabuild"
    mkdir -p "$work/classes" "$work/stage" "$work/doc"

    {
        printf 'Nordstjernen Java API build — %s\n' "$NVERSION"
        printf 'JAVA_HOME=%s\nCC=%s\nengine build dir=%s\n' \
            "$jhome" "${CC:-cc}" "$WORK/java-engine"
        "$jhome/bin/javac" -version 2>&1 || true
        printf -- '----------------------------------------\n'
    } | tee "$blog"

    log "Java: build native libraries (engine + JNI bridge)"
    mkdir -p "$work/stage/native"
    local nativeok=0
    if command -v "$DOCKER" >/dev/null 2>&1; then
        local jsrc="$WORK/javanative"
        mkdir -p "$jsrc"
        archive_to "$jsrc"
        local jtree="$jsrc/nordstjernen-${NVERSION}"
        if ! $DOCKER image inspect "$NIGHTLY_DEBIAN_IMAGE" >/dev/null 2>&1; then
            docker_pull "$NIGHTLY_DEBIAN_IMAGE" >> "$blog" 2>&1 || true
        fi
        if $DOCKER run --rm -v "$jtree:/build:z" -w /build \
                -e "CC=${CC:-cc}" "$NIGHTLY_DEBIAN_IMAGE" \
                bash scripts/nightly-java-native.sh >> "$blog" 2>&1 \
           && [ -d "$jtree/java/src/main/resources/native" ]; then
            cp -r "$jtree/java/src/main/resources/native/." "$work/stage/native/"
            nativeok=1
        fi
        rm -rf "$jsrc"
    fi
    if [ "$nativeok" != 1 ]; then
        log "Java: container native build unavailable; falling back to host toolchain"
        if JAVA_HOME="$jhome" BUILDDIR="$WORK/java-engine" CC="${CC:-cc}" \
                bash "$ROOT/java/scripts/build-native.sh" >> "$blog" 2>&1 \
           && [ -d "$ROOT/java/src/main/resources/native" ]; then
            cp -r "$ROOT/java/src/main/resources/native/." "$work/stage/native/"
            nativeok=1
        fi
    fi
    if [ "$nativeok" != 1 ]; then
        dump_tail "$blog"
        fail "$key" "native build failed (engine + JNI bridge) — see $blog"
        return
    fi
    log "Java: javac"
    if ! "$jhome/bin/javac" -d "$work/classes" \
            $(find "$ROOT/java/src/main/java" -name '*.java') >> "$blog" 2>&1; then
        dump_tail "$blog"
        fail "$key" "javac failed — see $blog"
        return
    fi

    cp -r "$work/classes/." "$work/stage/"
    if [ -d "$ROOT/java/src/main/resources/org" ]; then
        cp -r "$ROOT/java/src/main/resources/org" "$work/stage/"
    fi
    printf 'Automatic-Module-Name: org.nordstjernen\nEnable-Native-Access: ALL-UNNAMED\nMain-Class: org.nordstjernen.app.Browser\nImplementation-Title: Nordstjernen\nImplementation-Version: %s\n' \
        "$MESON_VERSION" > "$work/mf.txt"

    local base="nordstjernen-java-${NVERSION}"
    log "Java: fat jar (library API + browser app + icons + native libs)"
    if ! "$jhome/bin/jar" --create --file "$dst/${base}.jar" \
             --manifest "$work/mf.txt" -C "$work/stage" . >> "$blog" 2>&1 \
       || ! "$jhome/bin/jar" --create --file "$dst/${base}-sources.jar" \
             -C "$ROOT/java/src/main/java" . >> "$blog" 2>&1; then
        dump_tail "$blog"
        fail "$key" "jar failed — see $blog"
        return
    fi

    log "Java: javadoc"
    if "$jhome/bin/javadoc" -quiet -Xdoclint:none -d "$work/doc" \
            -sourcepath "$ROOT/java/src/main/java" org.nordstjernen >> "$blog" 2>&1; then
        "$jhome/bin/jar" --create --file "$dst/${base}-javadoc.jar" -C "$work/doc" . >> "$blog" 2>&1 || true
        rm -rf "$dst/apidocs"
        cp -r "$work/doc" "$dst/apidocs"
    else
        dump_tail "$blog"
        fail "$key" "javadoc failed — see $blog"
        return
    fi

    ln -sfn "java/${base}.jar"         "$OUTDIR/nordstjernen-java.jar"
    ln -sfn "java/${base}-sources.jar" "$OUTDIR/nordstjernen-java-sources.jar"
    ln -sfn "java/${base}-javadoc.jar" "$OUTDIR/nordstjernen-java-javadoc.jar"
    ok "$key"
}

link_stable() {
    local name="$1" pattern="$2" matches
    shopt -s nullglob
    matches=( "$OUTDIR"/$pattern )
    shopt -u nullglob
    if [ "${#matches[@]}" -gt 0 ]; then
        ln -sfn "${matches[0]#"$OUTDIR"/}" "$OUTDIR/$name"
        printf '  %-32s -> %s\n' "$name" "${matches[0]#"$OUTDIR"/}"
    fi
}

stage_stable_links() {
    log "Stable download links (/nightly/)"
    link_stable nordstjernen-windows-x86_64.zip  'windows/*/*-windows-x86_64.zip'
    link_stable nordstjernen-windows-x86_64.msix 'windows/*/*.msix'
    link_stable nordstjernen-windows-x86_64.exe  'windows/*/nordstjernen.exe'
    link_stable nordstjernen-macos.dmg           'macos/*/*.dmg'
    link_stable nordstjernen-macos-arm64         'macos/*/nordstjernen'
    link_stable nordstjernen-debian-amd64.deb    'linux/debian/*.deb'
    link_stable nordstjernen-ubuntu-amd64.deb    'linux/ubuntu/*.deb'
    link_stable nordstjernen-opensuse-x86_64.rpm 'linux/opensuse/*.rpm'
    link_stable nordstjernen-linux-x86_64.zip    'linux/ubuntu/*-linux-x86_64.zip'
    link_stable nordstjernen-alpine-x86_64.zip   'linux/alpine/*-linux-x86_64.zip'
    link_stable nordstjernen-alpine-x86_64.apk   'linux/alpine/*.apk'
    link_stable nordstjernen-freebsd-x86_64.zip  'freebsd/*/*-freebsd-x86_64.zip'
    link_stable nordstjernen-netbsd-x86_64.zip   'netbsd/*/*-netbsd-x86_64.zip'
    link_stable nordstjernen-src.tar.xz          'source/*.tar.xz'
    link_stable nordstjernen-src.tar.gz          'source/*.tar.gz'
}

# Dispatch the remote (Windows/macOS) builds first so they run on GitHub's
# hosted runners while the local container builds compile, then collect their
# artifacts once the local work is done.
if [ "$DO_GHA" = 1 ]; then
    gha_dispatch windows.yml windows
    gha_dispatch macos.yml   macos
    gha_dispatch freebsd.yml freebsd
    gha_dispatch netbsd.yml  netbsd
else
    skip "gha-windows"; skip "gha-macos"
    skip "gha-freebsd"; skip "gha-netbsd"
fi

[ "$DO_TARBALL" = 1 ] && stage_tarball || skip "source-tarball"

if [ "$DO_DOCKER" = 1 ]; then
    if command -v "$DOCKER" >/dev/null 2>&1; then
        run_distro_builds
    else
        fail "linux-debian" "$DOCKER not found"
        fail "linux-ubuntu" "$DOCKER not found"
        fail "linux-opensuse" "$DOCKER not found"
        fail "linux-alpine" "$DOCKER not found"
    fi
else
    skip "linux-debian"; skip "linux-ubuntu"; skip "linux-opensuse"
    skip "linux-alpine"
fi

if [ "$DO_GHA" = 1 ]; then
    gha_collect windows
    gha_collect macos
    gha_collect freebsd
    gha_collect netbsd
fi

[ "$DO_JAVA" = 1 ] && stage_java || skip "java"

stage_stable_links

log "Checksums"
( cd "$OUTDIR" && find . -type f ! -name SHA256SUMS ! -name nightly.log \
    -exec sha256sum {} + > SHA256SUMS ) && ok "checksums" || fail "checksums"

status_state() { cut -f1 "$STATUSDIR/$1" 2>/dev/null; }
status_keys()  { ls -1 "$STATUSDIR" 2>/dev/null | sort; }

log "Manifest"
{
    echo "Nordstjernen nightly build"
    echo "date:    $DATE"
    echo "ref:     $NIGHTLY_REF"
    echo "commit:  $COMMIT"
    echo "version: $NVERSION"
    echo "built:   $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo
    echo "Stage status:"
    for k in $(status_keys); do
        printf '  %-18s %s\n' "$k" "$(status_state "$k")"
    done
    echo
    echo "Artifacts:"
    ( cd "$OUTDIR" && find . -type f ! -name nightly.log | sort \
        | while read -r f; do printf '  %8s  %s\n' "$(du -h "$f" | cut -f1)" "$f"; done )
} > "$OUTDIR/MANIFEST.txt"
cat "$OUTDIR/MANIFEST.txt"

log "Summary"
rc=0
for k in $(status_keys); do
    st=$(status_state "$k")
    printf '  %-18s %s\n' "$k" "$st"
    [ "$st" = FAILED ] && rc=1
done
printf '\nOutput: %s\n' "$OUTDIR"
exit $rc
