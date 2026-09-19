/* Nordstjernen — CSS transitions and @keyframes animation engine.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "anim.h"

#include <math.h>
#include <string.h>

#define NS_ANIM_MAX_ACTIVE 256
#define NS_ANIM_KEYFRAME_PROP -1

typedef struct ns_anim_chan {
    int           prop;
    ns_css_value *last;
    ns_css_value *from;
    ns_css_value *to;
    ns_css_value *current;
    gboolean      active;
    gboolean      discrete;
    gboolean      started;
    gboolean      paused;
    gboolean      finished;
    double        start_us;
    double        duration_ms;
    double        delay_ms;
    double        paused_elapsed_ms;
    ns_css_timing timing;
    guint         generation;
} ns_anim_chan;

typedef struct ns_anim_kf_stop {
    double  pct;
    GArray *decls;
} ns_anim_kf_stop;

#define NS_ANIM_SCRIPT_BASE 1000

typedef struct ns_anim_run {
    int      index;
    gboolean is_script;
    char    *name;
    char    *cancelled_name;
    gboolean active;
    gboolean paused;
    gboolean started;
    gboolean finished;
    gboolean pending;
    int      iters_emitted;
    double   elapsed_base_ms;
    ns_css_keyframes *kf;
    GArray  *stops;
    double   start_us;
    double   duration_ms;
    double   delay_ms;
    double   iterations;
    ns_css_anim_direction direction;
    ns_css_anim_fill      fill;
    ns_css_timing timing;
    guint    generation;
    GHashTable *values;
} ns_anim_run;

typedef struct ns_anim_state {
    const ns_node *node;
    GPtrArray     *chans;
    GPtrArray     *runs;
    GPtrArray     *scripts;
    ns_style      *prev_style;
    gboolean       has_transition;
    guint          run_generation;
} ns_anim_state;

typedef struct {
    const ns_node *node;
    const char    *type;
    char          *name;
    double         elapsed_ms;
} ns_anim_event;

struct ns_anim {
    GHashTable *states;
    GHashTable *active;
    GHashTable *keyframes;
    int         active_count;
    GArray     *events;
    gint64      now_us;
};

static gint64
anim_now(ns_anim *a)
{
    if (a->now_us == 0) a->now_us = g_get_monotonic_time();
    return a->now_us;
}

static void
anim_emit(ns_anim *a, const ns_node *node, const char *type,
          const char *name, double elapsed_ms)
{
    if (!a || !node) return;
    if (!a->events)
        a->events = g_array_new(FALSE, FALSE, sizeof(ns_anim_event));
    ns_anim_event e = { node, type, g_strdup(name ? name : ""), elapsed_ms };
    g_array_append_val(a->events, e);
}

void
ns_anim_drain_events(ns_anim *a, ns_anim_event_cb cb, gpointer user)
{
    if (!a || !a->events || a->events->len == 0) return;
    GArray *evs = a->events;
    a->events = NULL;
    for (guint i = 0; i < evs->len; i++) {
        ns_anim_event *e = &g_array_index(evs, ns_anim_event, i);
        if (cb) cb(e->node, e->type, e->name, e->elapsed_ms, user);
        g_free(e->name);
    }
    g_array_free(evs, TRUE);
}

static void
chan_free(gpointer data)
{
    ns_anim_chan *ch = data;
    ns_css_value_free(ch->last);
    ns_css_value_free(ch->from);
    ns_css_value_free(ch->to);
    ns_css_value_free(ch->current);
    g_free(ch);
}

static void
anim_stops_free(GArray *stops)
{
    if (!stops) return;
    for (guint i = 0; i < stops->len; i++)
        ns_css_declarations_free(g_array_index(stops, ns_anim_kf_stop, i).decls);
    g_array_free(stops, TRUE);
}

static void
run_free(gpointer data)
{
    ns_anim_run *r = data;
    if (!r) return;
    ns_css_keyframes_resolved_free(r->kf);
    anim_stops_free(r->stops);
    g_free(r->name);
    g_free(r->cancelled_name);
    if (r->values) g_hash_table_destroy(r->values);
    g_free(r);
}

static void
ns_anim_state_free(gpointer data)
{
    ns_anim_state *s = data;
    if (!s) return;
    if (s->chans) g_ptr_array_free(s->chans, TRUE);
    if (s->runs) g_ptr_array_free(s->runs, TRUE);
    if (s->scripts) g_ptr_array_free(s->scripts, TRUE);
    ns_style_free(s->prev_style);
    g_free(s);
}

static void
ns_anim_keyframes_free(gpointer data)
{
    ns_css_keyframes *kf = data;
    if (!kf) return;
    g_free(kf->name);
    for (int i = 0; i < kf->n_stops; i++)
        g_free(kf->stops[i].raw_props);
    g_free(kf->stops);
    g_free(kf);
}

static gboolean advance_run(ns_anim *a, ns_anim_run *r, gint64 now_us);
static gboolean advance_chan(ns_anim *a, ns_anim_state *s, ns_anim_chan *ch, gint64 now_us);
static void run_emit_script(ns_anim *a, ns_anim_state *s, ns_anim_run *r,
                            const char *type);

static GPtrArray *
state_runs(const ns_anim_state *s, int which)
{
    return which == 0 ? s->runs : s->scripts;
}

static gboolean
state_is_active(const ns_anim_state *s)
{
    for (int w = 0; w < 2; w++) {
        GPtrArray *runs = state_runs(s, w);
        if (!runs) continue;
        for (guint i = 0; i < runs->len; i++) {
            const ns_anim_run *r = runs->pdata[i];
            if (r->active && !r->paused) return TRUE;
        }
    }
    if (s->chans)
        for (guint i = 0; i < s->chans->len; i++) {
            const ns_anim_chan *ch = s->chans->pdata[i];
            if (ch->active && !ch->paused) return TRUE;
        }
    return FALSE;
}

static void
anim_track(ns_anim *a, ns_anim_state *s)
{
    if (state_is_active(s)) g_hash_table_add(a->active, s);
    else                    g_hash_table_remove(a->active, s);
}

ns_anim *
ns_anim_new(void)
{
    ns_anim *a = g_new0(ns_anim, 1);
    a->states    = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, ns_anim_state_free);
    a->active    = g_hash_table_new(g_direct_hash, g_direct_equal);
    a->keyframes = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, ns_anim_keyframes_free);
    return a;
}

void
ns_anim_free(ns_anim *a)
{
    if (!a) return;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, a->states);
    while (g_hash_table_iter_next(&it, &key, &val))
        ns_css_incremental_exclude(key, FALSE);
    g_hash_table_destroy(a->active);
    g_hash_table_destroy(a->states);
    g_hash_table_destroy(a->keyframes);
    if (a->events) {
        for (guint i = 0; i < a->events->len; i++)
            g_free(g_array_index(a->events, ns_anim_event, i).name);
        g_array_free(a->events, TRUE);
    }
    g_free(a);
}

static int
state_active_count(const ns_anim_state *s)
{
    int n = 0;
    for (int w = 0; w < 2; w++) {
        GPtrArray *runs = state_runs(s, w);
        if (!runs) continue;
        for (guint i = 0; i < runs->len; i++)
            if (((ns_anim_run *)runs->pdata[i])->active) n++;
    }
    if (s->chans)
        for (guint i = 0; i < s->chans->len; i++)
            if (((ns_anim_chan *)s->chans->pdata[i])->active) n++;
    return n;
}

void
ns_anim_prune(ns_anim *a, GHashTable *live)
{
    if (!a || !live) return;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, a->states);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        if (g_hash_table_contains(live, key)) continue;
        ns_anim_state *s = val;
        a->active_count -= state_active_count(s);
        if (a->active_count < 0) a->active_count = 0;
        g_hash_table_remove(a->active, s);
        ns_css_incremental_exclude(key, FALSE);
        g_hash_table_iter_remove(&it);
    }
}

void
ns_anim_rebase(ns_anim *a, gint64 base_us)
{
    if (!a) return;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, a->states);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_anim_state *s = val;
        for (int w = 0; w < 2; w++) {
            GPtrArray *runs = state_runs(s, w);
            if (!runs) continue;
            for (guint i = 0; i < runs->len; i++)
                ((ns_anim_run *)runs->pdata[i])->start_us = base_us;
        }
        if (s->chans)
            for (guint i = 0; i < s->chans->len; i++)
                ((ns_anim_chan *)s->chans->pdata[i])->start_us = base_us;
    }
}

static void
ns_anim_register_keyframes(ns_anim *a, const ns_css_keyframes *src)
{
    if (!a || !src || !src->name) return;
    ns_css_keyframes *copy = g_new0(ns_css_keyframes, 1);
    copy->name = g_strdup(src->name);
    copy->n_stops = src->n_stops;
    if (src->n_stops > 0) {
        copy->stops = g_new(ns_css_keyframe_stop, src->n_stops);
        memcpy(copy->stops, src->stops,
               src->n_stops * sizeof(ns_css_keyframe_stop));
        for (int i = 0; i < copy->n_stops; i++)
            copy->stops[i].raw_props = g_strdup(src->stops[i].raw_props);
    }
    g_hash_table_replace(a->keyframes, g_strdup(src->name), copy);
}

void
ns_anim_load_from_stylesheet(ns_anim *a, const ns_css_stylesheet *sh)
{
    if (!a || !sh || !sh->keyframes) return;
    for (guint i = 0; i < sh->keyframes->len; i++) {
        const ns_css_keyframes *kf =
            &g_array_index(sh->keyframes, ns_css_keyframes, i);
        ns_anim_register_keyframes(a, kf);
    }
}

static double
steps_apply(int n, ns_css_step_pos pos, double x)
{
    if (n < 1) n = 1;
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    int step = (int)floor(x * n);
    if (pos == NS_CSS_STEP_JUMP_START || pos == NS_CSS_STEP_JUMP_BOTH)
        step += 1;
    int jumps = n;
    if (pos == NS_CSS_STEP_JUMP_NONE) jumps = n > 1 ? n - 1 : 1;
    else if (pos == NS_CSS_STEP_JUMP_BOTH) jumps = n + 1;
    if (step < 0) step = 0;
    if (step > jumps) step = jumps;
    return (double)step / jumps;
}

static double
cubic_bezier_axis(double t, double p1, double p2)
{
    double mt = 1.0 - t;
    return 3.0 * mt * mt * t * p1 + 3.0 * mt * t * t * p2 + t * t * t;
}

static double
cubic_bezier_apply(const double cb[4], double x)
{
    double t = x;
    for (int i = 0; i < 8; i++) {
        double xt = cubic_bezier_axis(t, cb[0], cb[2]) - x;
        if (fabs(xt) < 1e-6) break;
        double mt = 1.0 - t;
        double d = 3.0 * mt * mt * cb[0]
                 + 6.0 * mt * t * (cb[2] - cb[0])
                 + 3.0 * t * t * (1.0 - cb[2]);
        if (fabs(d) < 1e-6) break;
        t -= xt / d;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
    }
    return cubic_bezier_axis(t, cb[1], cb[3]);
}

static double
timing_apply(ns_css_timing t, double x)
{
    if (t.kind == NS_CSS_TIMING_STEPS)
        return steps_apply(t.steps, t.step_pos, x);
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    switch (t.kind) {
    case NS_CSS_TIMING_LINEAR:      return x;
    case NS_CSS_TIMING_EASE_IN: {
        static const double cb[4] = { 0.42, 0.0, 1.0, 1.0 };
        return cubic_bezier_apply(cb, x);
    }
    case NS_CSS_TIMING_EASE_OUT: {
        static const double cb[4] = { 0.0, 0.0, 0.58, 1.0 };
        return cubic_bezier_apply(cb, x);
    }
    case NS_CSS_TIMING_EASE_IN_OUT: {
        static const double cb[4] = { 0.42, 0.0, 0.58, 1.0 };
        return cubic_bezier_apply(cb, x);
    }
    case NS_CSS_TIMING_CUBIC:       return cubic_bezier_apply(t.cb, x);
    case NS_CSS_TIMING_EASE:
    default: {
        static const double cb[4] = { 0.25, 0.1, 0.25, 1.0 };
        return cubic_bezier_apply(cb, x);
    }
    }
}

static ns_anim_state *
state_for(ns_anim *a, const ns_node *dom)
{
    ns_anim_state *s = g_hash_table_lookup(a->states, dom);
    if (s) return s;
    s = g_new0(ns_anim_state, 1);
    s->node = dom;
    g_hash_table_insert(a->states, (gpointer)dom, s);
    ns_css_incremental_exclude(dom, TRUE);
    return s;
}

static gboolean
should_skip_motion(void)
{
    return ns_css_get_reduced_motion() == NS_CSS_REDUCED_MOTION_REDUCE;
}

static ns_anim_chan *
chan_find(const ns_anim_state *s, int prop)
{
    if (!s->chans) return NULL;
    for (guint i = 0; i < s->chans->len; i++) {
        ns_anim_chan *ch = s->chans->pdata[i];
        if (ch->prop == prop) return ch;
    }
    return NULL;
}

static ns_anim_chan *
chan_ensure(ns_anim_state *s, int prop)
{
    ns_anim_chan *ch = chan_find(s, prop);
    if (ch) return ch;
    if (!s->chans) s->chans = g_ptr_array_new_with_free_func(chan_free);
    ch = g_new0(ns_anim_chan, 1);
    ch->prop = prop;
    g_ptr_array_add(s->chans, ch);
    return ch;
}

static int
entry_prop(const ns_css_anim_entry *e)
{
    switch (e->target) {
    case NS_CSS_ANIM_TARGET_OPACITY:   return NS_CSS_OPACITY;
    case NS_CSS_ANIM_TARGET_TRANSFORM: return NS_CSS_TRANSFORM;
    case NS_CSS_ANIM_TARGET_COLOR:     return NS_CSS_COLOR;
    case NS_CSS_ANIM_TARGET_BG_COLOR:  return NS_CSS_BACKGROUND_COLOR;
    case NS_CSS_ANIM_TARGET_OTHER:
        return e->name ? ns_css_prop_id(e->name) : -1;
    default:
        return -1;
    }
}

static gboolean
prop_transitionable(int prop)
{
    switch (prop) {
    case NS_CSS_TRANSITION:
    case NS_CSS_TRANSITION_PROPERTY:
    case NS_CSS_TRANSITION_DURATION:
    case NS_CSS_TRANSITION_DELAY:
    case NS_CSS_TRANSITION_TIMING_FUNCTION:
    case NS_CSS_ANIMATION:
    case NS_CSS_ANIMATION_NAME:
    case NS_CSS_ANIMATION_DURATION:
    case NS_CSS_ANIMATION_DELAY:
    case NS_CSS_ANIMATION_TIMING_FUNCTION:
    case NS_CSS_ANIMATION_ITERATION_COUNT:
    case NS_CSS_ANIMATION_DIRECTION:
    case NS_CSS_ANIMATION_FILL_MODE:
    case NS_CSS_ANIMATION_PLAY_STATE:
    case NS_CSS_DISPLAY:
    case NS_CSS_CONTENT:
        return FALSE;
    default:
        return prop >= 0 && prop < NS_CSS_PROP_COUNT;
    }
}

static gboolean
prop_discretely_animatable(int prop)
{
    return prop == NS_CSS_VISIBILITY;
}

static double
chan_elapsed_ms(const ns_anim_chan *ch, gint64 now_us)
{
    return ch->paused
        ? ch->paused_elapsed_ms
        : (now_us - ch->start_us) / 1000.0 - ch->delay_ms;
}

static void
chan_start(ns_anim *a, ns_anim_state *s, ns_anim_chan *ch,
           const ns_css_value *from, const ns_css_value *to,
           const ns_css_anim_entry *e, gint64 now_us, gboolean discrete)
{
    if (ch->active) {
        anim_emit(a, s->node, "transitioncancel",
                  ns_css_prop_name(ch->prop),
                  MAX(chan_elapsed_ms(ch, now_us), 0.0));
        if (a->active_count > 0) a->active_count--;
    }
    ns_css_value *new_from = ns_css_value_dup(from);
    ns_css_value *new_to = ns_css_value_dup(to);
    ns_css_value *new_current = ns_css_value_dup(from);
    ns_css_value_free(ch->from);
    ns_css_value_free(ch->to);
    ns_css_value_free(ch->current);
    ch->from = new_from;
    ch->to = new_to;
    ch->current = new_current;
    ch->active = TRUE;
    ch->started = FALSE;
    ch->paused = FALSE;
    ch->finished = FALSE;
    ch->discrete = discrete;
    ch->start_us = now_us;
    ch->duration_ms = e->duration_ms;
    ch->delay_ms = e->delay_ms;
    ch->timing = e->timing;
    ch->generation++;
    a->active_count++;
    anim_emit(a, s->node, "transitionrun", ns_css_prop_name(ch->prop), 0.0);
    if (ch->delay_ms < 0) advance_chan(a, s, ch, now_us);
}

static void
chan_cancel(ns_anim *a, ns_anim_state *s, ns_anim_chan *ch, gint64 now_us)
{
    anim_emit(a, s->node, "transitioncancel", ns_css_prop_name(ch->prop),
              MAX(chan_elapsed_ms(ch, now_us), 0.0));
    ch->active = FALSE;
    if (a->active_count > 0) a->active_count--;
}

static gboolean
ancestor_transitions_to(ns_anim *a, const ns_node *node, int prop,
                        const ns_css_value *cur)
{
    for (const ns_node *p = node ? node->parent : NULL; p; p = p->parent) {
        const ns_anim_state *ps = g_hash_table_lookup(a->states, p);
        if (!ps) continue;
        const ns_anim_chan *pc = chan_find(ps, prop);
        if (pc && pc->active &&
            (ns_css_value_equal(pc->to, cur) || ns_css_value_equal(pc->current, cur)))
            return TRUE;
    }
    return FALSE;
}

static void
observe_transition_prop(ns_anim *a, ns_anim_state *s, const ns_style *style,
                        int prop, const ns_css_anim_entry *e, gint64 now_us)
{
    const ns_css_value *cur = style->values[prop];
    ns_anim_chan *ch = chan_ensure(s, prop);
    if (cur && ch->current && cur == ch->current) return;
    if (!ch->last && s->prev_style && s->prev_style != style)
        ch->last = ns_css_value_dup(s->prev_style->values[prop]);
    if (!ch->last) {
        ch->last = ns_css_value_dup(cur);
        return;
    }
    if (ns_css_value_equal(cur, ch->last)) return;
    gboolean can_run = e && cur && ch->last &&
                       e->duration_ms + e->delay_ms > 0 &&
                       e->duration_ms >= 0 && !should_skip_motion() &&
                       (ch->active || a->active_count < NS_ANIM_MAX_ACTIVE);
    if (can_run) {
        const ns_css_value *from = ch->active ? ch->current : ch->last;
        ns_css_value *probe = ns_css_value_interpolate(from, cur, 0.0);
        gboolean interpolable = probe != NULL;
        ns_css_value_free(probe);
        if (interpolable && ancestor_transitions_to(a, s->node, prop, cur)) {
            if (ch->active) chan_cancel(a, s, ch, now_us);
        } else if (interpolable) {
            chan_start(a, s, ch, from, cur, e, now_us, FALSE);
        } else if (prop_discretely_animatable(prop) || e->allow_discrete) {
            chan_start(a, s, ch, from, cur, e, now_us, TRUE);
        } else if (ch->active) {
            chan_cancel(a, s, ch, now_us);
        }
    } else if (ch->active) {
        chan_cancel(a, s, ch, now_us);
    }
    ns_css_value_free(ch->last);
    ch->last = ns_css_value_dup(cur);
}

static void
observe_transition(ns_anim *a, ns_anim_state *s, const ns_style *style,
                   const ns_css_anim_list *tv, gint64 now_us)
{
    gboolean has_list = tv->n > 0;
    s->has_transition = has_list;
    const ns_css_anim_entry *by_prop[NS_CSS_PROP_COUNT];
    memset(by_prop, 0, sizeof by_prop);
    gboolean touched[NS_CSS_PROP_COUNT];
    memset(touched, 0, sizeof touched);
    GArray *order = g_array_new(FALSE, FALSE, sizeof(int));
    for (int i = 0; i < tv->n; i++) {
        const ns_css_anim_entry *e = &tv->entries[i];
        if (e->target == NS_CSS_ANIM_TARGET_ALL) {
            for (int p = 0; p < NS_CSS_PROP_COUNT; p++) {
                if (!prop_transitionable(p)) continue;
                by_prop[p] = e;
                if (!touched[p]) { touched[p] = TRUE; g_array_append_val(order, p); }
            }
            continue;
        }
        int p = entry_prop(e);
        if (!prop_transitionable(p)) continue;
        by_prop[p] = e;
        if (!touched[p]) { touched[p] = TRUE; g_array_append_val(order, p); }
    }
    for (guint i = 0; i < order->len; i++) {
        int p = g_array_index(order, int, i);
        observe_transition_prop(a, s, style, p, by_prop[p], now_us);
    }
    if (s->chans) {
        for (guint i = 0; i < s->chans->len; i++) {
            ns_anim_chan *ch = s->chans->pdata[i];
            if (ch->prop >= 0 && ch->prop < NS_CSS_PROP_COUNT && !touched[ch->prop])
                observe_transition_prop(a, s, style, ch->prop, NULL, now_us);
        }
    }
    g_array_free(order, TRUE);
}

static GArray *
anim_stops_build(const ns_css_keyframes *kf)
{
    GArray *stops = g_array_new(FALSE, TRUE, sizeof(ns_anim_kf_stop));
    if (!kf) return stops;
    for (int i = 0; i < kf->n_stops; i++) {
        ns_anim_kf_stop st = { kf->stops[i].pct,
                               ns_css_parse_declarations(kf->stops[i].raw_props) };
        g_array_append_val(stops, st);
    }
    return stops;
}

static double
run_elapsed_ms(const ns_anim_run *r, gint64 now_us)
{
    if (r->paused) return r->elapsed_base_ms;
    if (r->pending) return -r->delay_ms;
    return (now_us - r->start_us) / 1000.0 - r->delay_ms;
}

static void
run_set_paused(ns_anim_run *r, gboolean paused, gint64 now_us)
{
    if (paused && !r->paused) {
        r->elapsed_base_ms = MAX(run_elapsed_ms(r, now_us), 0.0);
        r->paused = TRUE;
    } else if (!paused && r->paused) {
        r->start_us = now_us - (r->elapsed_base_ms + r->delay_ms) * 1000.0;
        r->paused = FALSE;
    }
}

static void
run_cancel(ns_anim *a, ns_anim_state *s, ns_anim_run *r)
{
    if (r->active) {
        r->active = FALSE;
        if (a->active_count > 0) a->active_count--;
    }
    if (r->name) {
        if (r->is_script) run_emit_script(a, s, r, "__nscancel");
        else anim_emit(a, s->node, "animationcancel", r->name, 0.0);
        g_free(r->name);
        r->name = NULL;
    }
    r->finished = FALSE;
    r->started = FALSE;
    r->pending = FALSE;
    if (r->values) g_hash_table_remove_all(r->values);
}

static void
run_configure(ns_anim_run *r, const ns_css_anim_entry *e)
{
    r->duration_ms = e->duration_ms;
    r->delay_ms = e->delay_ms;
    r->iterations = e->iterations;
    r->direction = e->direction;
    r->fill = e->fill;
    r->timing = e->timing;
}

static void
run_start(ns_anim *a, ns_anim_state *s, ns_anim_run *r,
          const ns_css_anim_entry *e, const ns_style *style, gint64 now_us)
{
    if (!r->active) {
        if (a->active_count >= NS_ANIM_MAX_ACTIVE) return;
        a->active_count++;
    }
    g_free(r->name);
    r->name = g_strdup(e->name);
    g_free(r->cancelled_name);
    r->cancelled_name = NULL;
    r->start_us = now_us;
    run_configure(r, e);
    r->active = TRUE;
    r->paused = e->paused;
    r->pending = !e->paused;
    r->started = FALSE;
    r->finished = FALSE;
    r->iters_emitted = 0;
    r->elapsed_base_ms = 0;
    r->generation = ++s->run_generation;
    ns_css_keyframes_resolved_free(r->kf);
    anim_stops_free(r->stops);
    const ns_css_keyframes *gkf = g_hash_table_lookup(a->keyframes, e->name);
    r->kf = gkf ? ns_css_keyframes_resolve(gkf, style->vars) : NULL;
    r->stops = anim_stops_build(r->kf ? r->kf : gkf);
    if (r->values) g_hash_table_remove_all(r->values);
    advance_run(a, r, now_us);
}

static void
observe_animation(ns_anim *a, ns_anim_state *s, const ns_style *style,
                  const ns_css_anim_list *av, gint64 now_us)
{
    int n = should_skip_motion() ? 0 : av->n;
    if (!s->runs) {
        if (n == 0) return;
        s->runs = g_ptr_array_new_with_free_func(run_free);
    }
    for (int i = 0; i < n; i++) {
        const ns_css_anim_entry *e = &av->entries[i];
        while ((int)s->runs->len <= i) {
            ns_anim_run *nr = g_new0(ns_anim_run, 1);
            nr->index = (int)s->runs->len;
            g_ptr_array_add(s->runs, nr);
        }
        ns_anim_run *r = s->runs->pdata[i];
        if (!e->name || e->duration_ms < 0 ||
            !g_hash_table_contains(a->keyframes, e->name)) {
            if (r->name) run_cancel(a, s, r);
            g_free(r->cancelled_name);
            r->cancelled_name = NULL;
            continue;
        }
        if (r->cancelled_name) {
            if (strcmp(r->cancelled_name, e->name) == 0) continue;
            g_free(r->cancelled_name);
            r->cancelled_name = NULL;
        }
        if (r->name && strcmp(r->name, e->name) == 0) {
            run_configure(r, e);
            run_set_paused(r, e->paused, now_us);
            continue;
        }
        if (r->name) run_cancel(a, s, r);
        run_start(a, s, r, e, style, now_us);
    }
    for (guint i = n; i < s->runs->len; i++) {
        ns_anim_run *r = s->runs->pdata[i];
        if (r->name) run_cancel(a, s, r);
        g_free(r->cancelled_name);
        r->cancelled_name = NULL;
    }
    while (s->runs->len > 0) {
        ns_anim_run *last = s->runs->pdata[s->runs->len - 1];
        if (last->name || last->cancelled_name) break;
        g_ptr_array_remove_index(s->runs, s->runs->len - 1);
    }
}

void
ns_anim_observe(ns_anim *a, const ns_node *dom,
                const ns_style *style, gint64 now_us)
{
    if (!a || !dom || !style) return;
    if (a->now_us == 0 || g_hash_table_size(a->active) == 0) a->now_us = now_us;
    now_us = a->now_us;
    ns_css_anim_list tv, av;
    ns_css_anim_effective(style, FALSE, &tv);
    ns_css_anim_effective(style, TRUE, &av);
    ns_anim_state *s = g_hash_table_lookup(a->states, dom);
    if (!s) {
        if (tv.n == 0 && av.n == 0) {
            ns_css_anim_list_clear(&tv);
            ns_css_anim_list_clear(&av);
            return;
        }
        s = state_for(a, dom);
    }
    observe_transition(a, s, style, &tv, now_us);
    observe_animation(a, s, style, &av, now_us);
    anim_track(a, s);
    ns_css_anim_list_clear(&tv);
    ns_css_anim_list_clear(&av);
    if (s->prev_style != style) {
        ns_style_free(s->prev_style);
        s->prev_style = (ns_style *)style;
        s->prev_style->ref++;
    }
}

static int
node_depth(const ns_node *n)
{
    int d = 0;
    for (const ns_node *p = n ? n->parent : NULL; p; p = p->parent) d++;
    return d;
}

typedef struct {
    const ns_node  *node;
    const ns_style *style;
    int             depth;
} ns_anim_observe_item;

static int
observe_item_cmp(gconstpointer x, gconstpointer y)
{
    const ns_anim_observe_item *p = x, *q = y;
    return p->depth - q->depth;
}

void
ns_anim_observe_all(ns_anim *a, GHashTable *styles, gint64 now_us)
{
    if (!a || !styles) return;
    GArray *items = g_array_sized_new(FALSE, FALSE, sizeof(ns_anim_observe_item),
                                      g_hash_table_size(styles));
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, styles);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_anim_observe_item item = { key, val, node_depth(key) };
        g_array_append_val(items, item);
    }
    g_array_sort(items, observe_item_cmp);
    for (guint i = 0; i < items->len; i++) {
        const ns_anim_observe_item *item = &g_array_index(items, ns_anim_observe_item, i);
        ns_anim_observe(a, item->node, item->style, now_us);
    }
    g_array_free(items, TRUE);
    ns_anim_prune(a, styles);
    ns_anim_apply(a, styles);
}

static void
style_set_value(ns_style *st, int prop, ns_css_value *v)
{
    if (!st || prop < 0 || prop >= NS_CSS_PROP_COUNT || !v) return;
    if (st->values[prop] == v) return;
    ns_css_value_free(st->values[prop]);
    st->values[prop] = ns_css_value_dup(v);
}

static void
apply_propagate(GHashTable *styles, const ns_node *node, int prop,
                const ns_css_value *base, ns_css_value *current)
{
    for (const ns_node *c = node->first_child; c; c = c->next_sibling) {
        ns_style *st = g_hash_table_lookup(styles, c);
        if (!st) continue;
        const ns_css_value *v = st->values[prop];
        if (v != base && !(v && base && ns_css_value_equal(v, base))) continue;
        style_set_value(st, prop, current);
        apply_propagate(styles, c, prop, base, current);
    }
}

static void
apply_animated_value(GHashTable *styles, const ns_node *node, ns_style *st,
                     int prop, ns_css_value *current)
{
    if (prop < 0 || prop >= NS_CSS_PROP_COUNT || !current) return;
    ns_css_value *base = st->values[prop];
    if (base == current) return;
    st->values[prop] = ns_css_value_dup(current);
    if (base) apply_propagate(styles, node, prop, base, current);
    ns_css_value_free(base);
}

void
ns_anim_apply(ns_anim *a, GHashTable *styles)
{
    if (!a || !styles) return;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, a->states);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_anim_state *s = val;
        ns_style *st = g_hash_table_lookup(styles, key);
        if (!st) continue;
        if (s->chans)
            for (guint i = 0; i < s->chans->len; i++) {
                ns_anim_chan *ch = s->chans->pdata[i];
                if (ch->active && ch->current)
                    apply_animated_value(styles, key, st, ch->prop, ch->current);
            }
        for (int w = 0; w < 2; w++) {
            GPtrArray *runs = state_runs(s, w);
            if (!runs) continue;
            for (guint i = 0; i < runs->len; i++) {
                ns_anim_run *r = runs->pdata[i];
                if (!r->values) continue;
                GHashTableIter vit;
                gpointer vk, vv;
                g_hash_table_iter_init(&vit, r->values);
                while (g_hash_table_iter_next(&vit, &vk, &vv))
                    apply_animated_value(styles, key, st, GPOINTER_TO_INT(vk), vv);
            }
        }
    }
}

static gboolean
advance_chan(ns_anim *a, ns_anim_state *s, ns_anim_chan *ch, gint64 now_us)
{
    double elapsed = chan_elapsed_ms(ch, now_us);
    if (elapsed < 0) {
        if (ch->current != ch->from) {
            ns_css_value_free(ch->current);
            ch->current = ns_css_value_dup(ch->from);
        }
        return TRUE;
    }
    if (!ch->started) {
        ch->started = TRUE;
        anim_emit(a, s->node, "transitionstart", ns_css_prop_name(ch->prop), 0.0);
    }
    if (elapsed >= ch->duration_ms) {
        ns_css_value_free(ch->current);
        ch->current = ns_css_value_dup(ch->to);
        ch->active = FALSE;
        ch->finished = TRUE;
        if (a->active_count > 0) a->active_count--;
        anim_emit(a, s->node, "transitionend", ns_css_prop_name(ch->prop),
                  ch->duration_ms);
        return TRUE;
    }
    double t = ch->duration_ms > 0 ? elapsed / ch->duration_ms : 1.0;
    double eased = timing_apply(ch->timing, t);
    ns_css_value *next = ch->discrete
        ? ns_css_value_dup(eased < 0.5 ? ch->from : ch->to)
        : ns_css_value_interpolate(ch->from, ch->to, eased);
    if (!next) next = ns_css_value_dup(eased < 0.5 ? ch->from : ch->to);
    ns_css_value_free(ch->current);
    ch->current = next;
    return TRUE;
}

static const ns_css_value *
stop_value(const ns_anim_kf_stop *st, int prop)
{
    if (!st->decls) return NULL;
    const ns_css_value *found = NULL;
    for (guint i = 0; i < st->decls->len; i++) {
        const ns_css_decl *d = &g_array_index(st->decls, ns_css_decl, i);
        if ((int)d->prop == prop) found = d->value;
    }
    return found;
}

static void
run_sample_at(ns_anim_run *r, double progress)
{
    double pct = timing_apply(r->timing, progress) * 100.0;
    if (!r->values)
        r->values = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                          NULL, (GDestroyNotify)ns_css_value_free);
    g_hash_table_remove_all(r->values);
    if (!r->stops) return;
    gboolean seen[NS_CSS_PROP_COUNT];
    memset(seen, 0, sizeof seen);
    for (guint i = 0; i < r->stops->len; i++) {
        const ns_anim_kf_stop *st = &g_array_index(r->stops, ns_anim_kf_stop, i);
        if (!st->decls) continue;
        for (guint d = 0; d < st->decls->len; d++) {
            int prop = (int)g_array_index(st->decls, ns_css_decl, d).prop;
            if (prop < 0 || prop >= NS_CSS_PROP_COUNT || seen[prop]) continue;
            seen[prop] = TRUE;
            const ns_anim_kf_stop *prev = NULL, *next = NULL;
            for (guint k = 0; k < r->stops->len; k++) {
                const ns_anim_kf_stop *c = &g_array_index(r->stops, ns_anim_kf_stop, k);
                if (!stop_value(c, prop)) continue;
                if (c->pct <= pct) prev = c;
                if (c->pct >= pct && !next) next = c;
            }
            ns_css_value *out = NULL;
            if (prev && next && prev != next) {
                double range = next->pct - prev->pct;
                double t = range > 0 ? (pct - prev->pct) / range : 0;
                out = ns_css_value_interpolate(stop_value(prev, prop),
                                               stop_value(next, prop), t);
                if (!out)
                    out = ns_css_value_dup(t < 0.5 ? stop_value(prev, prop)
                                                   : stop_value(next, prop));
            } else if (prev || next) {
                out = ns_css_value_dup(stop_value(prev ? prev : next, prop));
            }
            if (out) g_hash_table_insert(r->values, GINT_TO_POINTER(prop), out);
        }
    }
}

static double
directed_progress(int iter, double raw, ns_css_anim_direction dir)
{
    gboolean rev;
    switch (dir) {
    case NS_CSS_ANIM_DIR_REVERSE:           rev = TRUE; break;
    case NS_CSS_ANIM_DIR_ALTERNATE:         rev = (iter & 1); break;
    case NS_CSS_ANIM_DIR_ALTERNATE_REVERSE: rev = !(iter & 1); break;
    case NS_CSS_ANIM_DIR_NORMAL:
    default:                                rev = FALSE; break;
    }
    return rev ? 1.0 - raw : raw;
}

static int
run_last_iteration(const ns_anim_run *r)
{
    if (!isfinite(r->iterations)) return 0;
    int last = (int)ceil(r->iterations) - 1;
    return last < 0 ? 0 : last;
}

static double
run_active_ms(const ns_anim_run *r)
{
    if (!isfinite(r->iterations)) return INFINITY;
    return r->duration_ms * r->iterations;
}

static gboolean
advance_run(ns_anim *a, ns_anim_run *r, gint64 now_us)
{
    if (!r->name) return FALSE;
    double elapsed = run_elapsed_ms(r, now_us);
    if (elapsed < 0) {
        gboolean fill_back = r->fill == NS_CSS_ANIM_FILL_BACKWARDS ||
                             r->fill == NS_CSS_ANIM_FILL_BOTH;
        if (!fill_back) {
            if (r->values) g_hash_table_remove_all(r->values);
            return FALSE;
        }
        run_sample_at(r, directed_progress(0, 0.0, r->direction));
        return TRUE;
    }
    double active = run_active_ms(r);
    if (r->duration_ms <= 0 || (isfinite(active) && elapsed >= active)) {
        if (r->active) {
            r->active = FALSE;
            r->finished = TRUE;
            if (a->active_count > 0) a->active_count--;
        }
        gboolean fill_fwd = r->fill == NS_CSS_ANIM_FILL_FORWARDS ||
                            r->fill == NS_CSS_ANIM_FILL_BOTH;
        if (!fill_fwd) {
            if (r->values) g_hash_table_remove_all(r->values);
            return TRUE;
        }
        int last = run_last_iteration(r);
        double raw_end = isfinite(r->iterations) ? r->iterations - last : 1.0;
        if (raw_end < 0) raw_end = 0;
        if (raw_end > 1) raw_end = 1;
        run_sample_at(r, directed_progress(last, raw_end, r->direction));
        return TRUE;
    }
    double iter_d = elapsed / r->duration_ms;
    if (iter_d > 1e9) iter_d = 1e9;
    int iter = (int)iter_d;
    double raw = fmod(elapsed, r->duration_ms) / r->duration_ms;
    run_sample_at(r, directed_progress(iter, raw, r->direction));
    return TRUE;
}

static void
run_emit_script(ns_anim *a, ns_anim_state *s, ns_anim_run *r, const char *type)
{
    char idx[16];
    g_snprintf(idx, sizeof idx, "%d", r->index);
    anim_emit(a, s->node, type, idx, 0.0);
}

static void
run_emit_progress(ns_anim *a, ns_anim_state *s, ns_anim_run *r, gint64 now_us)
{
    if (r->is_script) {
        if (!r->active) run_emit_script(a, s, r, "__nsfinish");
        return;
    }
    double el = run_elapsed_ms(r, now_us);
    if (!r->started && el >= 0) {
        r->started = TRUE;
        r->iters_emitted = 0;
        anim_emit(a, s->node, "animationstart", r->name, 0.0);
    }
    if (r->started && r->duration_ms > 0) {
        int reached = (int)MIN(el / r->duration_ms, 1e9);
        int last = run_last_iteration(r);
        if (isfinite(r->iterations) && reached > last) reached = last;
        while (r->iters_emitted < reached) {
            r->iters_emitted++;
            anim_emit(a, s->node, "animationiteration", r->name,
                      r->iters_emitted * r->duration_ms);
        }
    }
    if (!r->active) {
        double active = run_active_ms(r);
        anim_emit(a, s->node, "animationend", r->name,
                  isfinite(active) ? active : r->duration_ms);
        r->started = FALSE;
        r->iters_emitted = 0;
    }
}

gboolean
ns_anim_tick(ns_anim *a, gint64 now_us)
{
    if (!a) return FALSE;
    a->now_us = now_us;
    if (g_hash_table_size(a->active) == 0) return FALSE;
    gboolean any = FALSE;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, a->active);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_anim_state *s = key;
        if (s->chans)
            for (guint i = 0; i < s->chans->len; i++) {
                ns_anim_chan *ch = s->chans->pdata[i];
                if (ch->active && !ch->paused && advance_chan(a, s, ch, now_us))
                    any = TRUE;
            }
        for (int w = 0; w < 2; w++) {
            GPtrArray *runs = state_runs(s, w);
            if (!runs) continue;
            for (guint i = 0; i < runs->len; i++) {
                ns_anim_run *r = runs->pdata[i];
                if (!r->active) continue;
                if (r->pending) {
                    r->pending = FALSE;
                    r->start_us = now_us;
                }
                if (advance_run(a, r, now_us)) any = TRUE;
                run_emit_progress(a, s, r, now_us);
            }
        }
        if (!state_is_active(s)) g_hash_table_iter_remove(&it);
    }
    return any;
}

gboolean
ns_anim_has_active(const ns_anim *a)
{
    return a && a->active && g_hash_table_size(a->active) > 0;
}

gboolean
ns_anim_needs_layout(const ns_anim *a)
{
    if (!a || !a->active) return FALSE;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, a->active);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        const ns_anim_state *s = key;
        if (s->chans)
            for (guint i = 0; i < s->chans->len; i++) {
                const ns_anim_chan *ch = s->chans->pdata[i];
                if (ch->active && ns_css_prop_affects_layout(ch->prop))
                    return TRUE;
            }
        for (int w = 0; w < 2; w++) {
            GPtrArray *runs = state_runs(s, w);
            if (!runs) continue;
            for (guint i = 0; i < runs->len; i++) {
                const ns_anim_run *r = runs->pdata[i];
                if (!r->values) continue;
                GHashTableIter vit;
                gpointer vk, vv;
                g_hash_table_iter_init(&vit, r->values);
                while (g_hash_table_iter_next(&vit, &vk, &vv))
                    if (ns_css_prop_affects_layout(GPOINTER_TO_INT(vk))) return TRUE;
            }
        }
    }
    return FALSE;
}

static const ns_css_value *
state_prop_value(const ns_anim_state *s, int prop)
{
    for (int w = 1; w >= 0; w--) {
        GPtrArray *runs = state_runs(s, w);
        if (!runs) continue;
        for (guint i = runs->len; i > 0; i--) {
            const ns_anim_run *r = runs->pdata[i - 1];
            if (!r->values) continue;
            const ns_css_value *v = g_hash_table_lookup(r->values,
                                                        GINT_TO_POINTER(prop));
            if (v) return v;
        }
    }
    const ns_anim_chan *ch = chan_find(s, prop);
    if (ch && ch->active) return ch->current;
    return NULL;
}

gboolean
ns_anim_get_opacity(ns_anim *a, const ns_node *dom, double *out_opacity)
{
    if (!a || !dom || !out_opacity) return FALSE;
    ns_anim_state *s = g_hash_table_lookup(a->states, dom);
    if (!s) return FALSE;
    const ns_css_value *v = state_prop_value(s, NS_CSS_OPACITY);
    if (!v || v->kind != NS_CSS_V_LENGTH) return FALSE;
    double o = v->u.length.v;
    if (v->u.length.unit == NS_CSS_UNIT_PERCENT) o /= 100.0;
    *out_opacity = CLAMP(o, 0.0, 1.0);
    return TRUE;
}

const ns_css_transform *
ns_anim_get_transform(ns_anim *a, const ns_node *dom)
{
    if (!a || !dom) return NULL;
    ns_anim_state *s = g_hash_table_lookup(a->states, dom);
    if (!s) return NULL;
    const ns_css_value *v = state_prop_value(s, NS_CSS_TRANSFORM);
    if (!v || v->kind != NS_CSS_V_TRANSFORM || v->u.transform.n_ops == 0)
        return NULL;
    return &v->u.transform;
}

gboolean
ns_anim_get_color(ns_anim *a, const ns_node *dom,
                  ns_css_anim_target which, guint8 out_rgba[4])
{
    if (!a || !dom || !out_rgba) return FALSE;
    ns_anim_state *s = g_hash_table_lookup(a->states, dom);
    if (!s) return FALSE;
    int prop = which == NS_CSS_ANIM_TARGET_COLOR ? NS_CSS_COLOR
             : which == NS_CSS_ANIM_TARGET_BG_COLOR ? NS_CSS_BACKGROUND_COLOR : -1;
    const ns_css_value *v = prop >= 0 ? state_prop_value(s, prop) : NULL;
    if (!v || v->kind != NS_CSS_V_COLOR) return FALSE;
    out_rgba[0] = v->u.color.r;
    out_rgba[1] = v->u.color.g;
    out_rgba[2] = v->u.color.b;
    out_rgba[3] = v->u.color.a;
    return TRUE;
}

static void
info_set_easing(ns_anim_info *out, const ns_css_timing *t)
{
    char *e = ns_css_timing_serialize(t);
    g_strlcpy(out->easing, e, sizeof out->easing);
    g_free(e);
}

static void
chan_info(const ns_anim_state *s, const ns_anim_chan *ch, gint64 now_us,
          ns_anim_info *out)
{
    memset(out, 0, sizeof *out);
    out->node = s->node;
    out->prop = ch->prop;
    out->run = -1;
    out->name = ns_css_prop_name(ch->prop);
    out->duration_ms = ch->duration_ms;
    out->delay_ms = ch->delay_ms;
    out->iterations = 1;
    out->fill = "backwards";
    out->direction = "normal";
    info_set_easing(out, &ch->timing);
    out->active = ch->active;
    out->paused = ch->paused;
    out->finished = ch->finished;
    out->generation = ch->generation;
    double el = chan_elapsed_ms(ch, now_us) + ch->delay_ms;
    if (ch->finished) el = ch->delay_ms + ch->duration_ms;
    out->current_ms = el;
}

static const char *
fill_name(ns_css_anim_fill f)
{
    switch (f) {
    case NS_CSS_ANIM_FILL_FORWARDS:  return "forwards";
    case NS_CSS_ANIM_FILL_BACKWARDS: return "backwards";
    case NS_CSS_ANIM_FILL_BOTH:      return "both";
    default:                         return "none";
    }
}

static const char *
direction_name(ns_css_anim_direction d)
{
    switch (d) {
    case NS_CSS_ANIM_DIR_REVERSE:           return "reverse";
    case NS_CSS_ANIM_DIR_ALTERNATE:         return "alternate";
    case NS_CSS_ANIM_DIR_ALTERNATE_REVERSE: return "alternate-reverse";
    default:                                return "normal";
    }
}

static void
run_info(const ns_anim_state *s, const ns_anim_run *r, gint64 now_us,
         ns_anim_info *out)
{
    memset(out, 0, sizeof *out);
    out->node = s->node;
    out->prop = NS_ANIM_KEYFRAME_PROP;
    out->run = r->index;
    out->name = r->is_script ? NULL : r->name;
    out->duration_ms = r->duration_ms;
    out->delay_ms = r->delay_ms;
    out->iterations = r->iterations;
    out->fill = fill_name(r->fill);
    out->direction = direction_name(r->direction);
    info_set_easing(out, &r->timing);
    out->active = r->active;
    out->paused = r->paused;
    out->finished = r->finished;
    out->generation = r->generation;
    double el = run_elapsed_ms(r, now_us) + r->delay_ms;
    if (r->finished && !r->paused) {
        double active = run_active_ms(r);
        el = MAX(el, r->delay_ms + (isfinite(active) ? active : 0));
    }
    out->current_ms = el;
}

static gboolean
run_in_effect(const ns_anim_run *r)
{
    if (!r->name) return FALSE;
    if (r->active) return TRUE;
    return r->finished && (r->fill == NS_CSS_ANIM_FILL_FORWARDS ||
                           r->fill == NS_CSS_ANIM_FILL_BOTH);
}

void
ns_anim_visit(ns_anim *a, const ns_node *node, ns_anim_visit_cb cb,
              gpointer user)
{
    if (!a || !cb) return;
    gint64 now = anim_now(a);
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, a->states);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_anim_state *s = val;
        if (node && s->node != node) continue;
        ns_anim_info info;
        for (int w = 0; w < 2; w++) {
            GPtrArray *runs = state_runs(s, w);
            if (!runs) continue;
            for (guint i = 0; i < runs->len; i++) {
                ns_anim_run *r = runs->pdata[i];
                if (!run_in_effect(r)) continue;
                run_info(s, r, now, &info);
                cb(&info, user);
            }
        }
        if (s->chans)
            for (guint i = 0; i < s->chans->len; i++) {
                ns_anim_chan *ch = s->chans->pdata[i];
                if (!ch->active) continue;
                chan_info(s, ch, now, &info);
                cb(&info, user);
            }
    }
}

static ns_anim_run *
run_for(const ns_anim_state *s, int prop)
{
    int index = -1 - prop;
    GPtrArray *runs = s->runs;
    if (index >= NS_ANIM_SCRIPT_BASE) {
        index -= NS_ANIM_SCRIPT_BASE;
        runs = s->scripts;
    }
    if (!runs || index < 0 || (guint)index >= runs->len) return NULL;
    ns_anim_run *r = runs->pdata[index];
    return (r->name || r->cancelled_name) ? r : NULL;
}

static ns_css_anim_direction
direction_from_name(const char *d)
{
    if (!d) return NS_CSS_ANIM_DIR_NORMAL;
    if (strcmp(d, "reverse") == 0) return NS_CSS_ANIM_DIR_REVERSE;
    if (strcmp(d, "alternate") == 0) return NS_CSS_ANIM_DIR_ALTERNATE;
    if (strcmp(d, "alternate-reverse") == 0) return NS_CSS_ANIM_DIR_ALTERNATE_REVERSE;
    return NS_CSS_ANIM_DIR_NORMAL;
}

static ns_css_anim_fill
fill_from_name(const char *f)
{
    if (!f) return NS_CSS_ANIM_FILL_NONE;
    if (strcmp(f, "forwards") == 0) return NS_CSS_ANIM_FILL_FORWARDS;
    if (strcmp(f, "backwards") == 0) return NS_CSS_ANIM_FILL_BACKWARDS;
    if (strcmp(f, "both") == 0) return NS_CSS_ANIM_FILL_BOTH;
    return NS_CSS_ANIM_FILL_NONE;
}

gboolean
ns_anim_script_start(ns_anim *a, const ns_node *node,
                     const char *const *stop_css, const double *stop_pct,
                     int n_stops, const ns_anim_script_timing *t,
                     int *out_prop, guint *out_generation)
{
    if (!a || !node || !t) return FALSE;
    if (a->active_count >= NS_ANIM_MAX_ACTIVE) return FALSE;
    ns_anim_state *s = state_for(a, node);
    if (!s->scripts) s->scripts = g_ptr_array_new_with_free_func(run_free);
    ns_anim_run *r = NULL;
    for (guint i = 0; i < s->scripts->len && !r; i++) {
        ns_anim_run *c = s->scripts->pdata[i];
        if (!c->name && !c->cancelled_name && !c->active) r = c;
    }
    if (!r) {
        r = g_new0(ns_anim_run, 1);
        r->index = NS_ANIM_SCRIPT_BASE + (int)s->scripts->len;
        r->is_script = TRUE;
        g_ptr_array_add(s->scripts, r);
    }
    g_free(r->name);
    r->name = g_strdup("");
    g_free(r->cancelled_name);
    r->cancelled_name = NULL;
    ns_css_keyframes_resolved_free(r->kf);
    r->kf = NULL;
    anim_stops_free(r->stops);
    r->stops = g_array_new(FALSE, TRUE, sizeof(ns_anim_kf_stop));
    for (int i = 0; i < n_stops; i++) {
        ns_anim_kf_stop st = { stop_pct[i] * 100.0,
                               ns_css_parse_declarations(stop_css[i]) };
        g_array_append_val(r->stops, st);
    }
    gint64 now = anim_now(a);
    r->start_us = now;
    r->duration_ms = MAX(t->duration_ms, 0.0);
    r->delay_ms = t->delay_ms;
    r->iterations = t->iterations < 0 ? 0 : t->iterations;
    r->direction = direction_from_name(t->direction);
    r->fill = fill_from_name(t->fill);
    r->timing = (ns_css_timing){ .kind = NS_CSS_TIMING_LINEAR };
    if (t->easing) ns_css_timing_parse(t->easing, &r->timing);
    r->active = TRUE;
    r->paused = FALSE;
    r->pending = TRUE;
    r->started = FALSE;
    r->finished = FALSE;
    r->iters_emitted = 0;
    r->elapsed_base_ms = 0;
    r->generation = ++s->run_generation;
    if (r->values) g_hash_table_remove_all(r->values);
    a->active_count++;
    advance_run(a, r, now);
    anim_track(a, s);
    if (out_prop) *out_prop = -1 - r->index;
    if (out_generation) *out_generation = r->generation;
    return TRUE;
}

gboolean
ns_anim_info_for(ns_anim *a, const ns_node *node, int prop, ns_anim_info *out)
{
    if (!a || !node || !out) return FALSE;
    ns_anim_state *s = g_hash_table_lookup(a->states, node);
    if (!s) return FALSE;
    gint64 now = anim_now(a);
    if (prop < 0) {
        ns_anim_run *r = run_for(s, prop);
        if (!r || !r->name) return FALSE;
        run_info(s, r, now, out);
        return TRUE;
    }
    ns_anim_chan *ch = chan_find(s, prop);
    if (!ch || (!ch->active && !ch->finished)) return FALSE;
    chan_info(s, ch, now, out);
    return TRUE;
}

gboolean
ns_anim_seek(ns_anim *a, const ns_node *node, int prop, double ms)
{
    if (!a || !node) return FALSE;
    ns_anim_state *s = g_hash_table_lookup(a->states, node);
    if (!s) return FALSE;
    gint64 now = anim_now(a);
    if (prop < 0) {
        ns_anim_run *r = run_for(s, prop);
        if (!r || !r->name) return FALSE;
        double el = ms - r->delay_ms;
        r->pending = FALSE;
        if (r->paused) r->elapsed_base_ms = MAX(el, 0.0);
        else r->start_us = now - ms * 1000.0;
        if (!r->active && r->finished) {
            r->active = TRUE;
            r->finished = FALSE;
            a->active_count++;
        }
        advance_run(a, r, now);
        anim_track(a, s);
        return TRUE;
    }
    ns_anim_chan *ch = chan_find(s, prop);
    if (!ch || (!ch->active && !ch->finished)) return FALSE;
    double el = ms - ch->delay_ms;
    if (ch->paused) ch->paused_elapsed_ms = el;
    else ch->start_us = now - ms * 1000.0;
    if (!ch->active) {
        ch->active = TRUE;
        ch->finished = FALSE;
        a->active_count++;
    }
    advance_chan(a, s, ch, now);
    anim_track(a, s);
    return TRUE;
}

gboolean
ns_anim_control(ns_anim *a, const ns_node *node, int prop, const char *op)
{
    if (!a || !node || !op) return FALSE;
    ns_anim_state *s = g_hash_table_lookup(a->states, node);
    if (!s) return FALSE;
    gint64 now = anim_now(a);
    if (prop < 0) {
        ns_anim_run *r = run_for(s, prop);
        if (!r) return FALSE;
        if (strcmp(op, "pause") == 0) {
            if (!r->name) return FALSE;
            if (r->pending) { r->pending = FALSE; r->start_us = now; }
            run_set_paused(r, TRUE, now);
        } else if (strcmp(op, "play") == 0) {
            if (!r->name) {
                r->name = r->cancelled_name;
                r->cancelled_name = NULL;
                r->active = TRUE;
                r->finished = FALSE;
                r->paused = FALSE;
                r->pending = TRUE;
                r->start_us = now;
                a->active_count++;
                if (r->values) g_hash_table_remove_all(r->values);
            } else {
                run_set_paused(r, FALSE, now);
                if (!r->active && r->finished) {
                    r->active = TRUE;
                    r->finished = FALSE;
                    a->active_count++;
                    r->start_us = now;
                }
            }
        } else if (strcmp(op, "finish") == 0) {
            if (!r->name) return FALSE;
            double active = run_active_ms(r);
            double total = r->delay_ms + (isfinite(active) ? active : 0);
            r->paused = FALSE;
            r->pending = FALSE;
            r->start_us = now - total * 1000.0;
            advance_run(a, r, now);
        } else if (strcmp(op, "cancel") == 0) {
            if (!r->name) return FALSE;
            char *keep = g_strdup(r->name);
            run_cancel(a, s, r);
            g_free(r->cancelled_name);
            r->cancelled_name = keep;
        }
        anim_track(a, s);
        return TRUE;
    }
    ns_anim_chan *ch = chan_find(s, prop);
    if (!ch || (!ch->active && !ch->finished)) return FALSE;
    if (strcmp(op, "pause") == 0 && !ch->paused) {
        ch->paused_elapsed_ms = chan_elapsed_ms(ch, now);
        ch->paused = TRUE;
    } else if (strcmp(op, "play") == 0 && ch->paused) {
        ch->start_us = now - (ch->paused_elapsed_ms + ch->delay_ms) * 1000.0;
        ch->paused = FALSE;
    } else if (strcmp(op, "finish") == 0) {
        ch->paused = FALSE;
        ch->start_us = now - (ch->delay_ms + ch->duration_ms) * 1000.0;
        if (ch->active) advance_chan(a, s, ch, now);
    } else if (strcmp(op, "cancel") == 0) {
        if (ch->active) chan_cancel(a, s, ch, now);
        ch->finished = FALSE;
    }
    anim_track(a, s);
    return TRUE;
}
