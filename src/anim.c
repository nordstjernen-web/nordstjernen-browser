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
    gint64        start_us;
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

typedef struct ns_anim_state {
    const ns_node *node;
    GPtrArray     *chans;
    gboolean       has_transition;

    gboolean anim_active;
    gboolean anim_paused;
    gboolean anim_started;
    gboolean anim_finished;
    int      anim_iters_emitted;
    double   anim_elapsed_base_ms;
    ns_css_keyframes *anim_kf;
    GArray  *anim_stops;
    char    *anim_name;
    gint64   anim_start_us;
    double   anim_duration_ms;
    double   anim_delay_ms;
    int      anim_iter_count;
    ns_css_anim_direction anim_direction;
    ns_css_anim_fill      anim_fill;
    ns_css_timing anim_timing;
    guint    anim_generation;
    GHashTable *anim_values;
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
};

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
ns_anim_state_free(gpointer data)
{
    ns_anim_state *s = data;
    if (!s) return;
    ns_css_keyframes_resolved_free(s->anim_kf);
    anim_stops_free(s->anim_stops);
    g_free(s->anim_name);
    if (s->chans) g_ptr_array_free(s->chans, TRUE);
    if (s->anim_values) g_hash_table_destroy(s->anim_values);
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

static gboolean advance_animation(ns_anim *a, ns_anim_state *s, gint64 now_us);

static gboolean
state_is_active(const ns_anim_state *s)
{
    if (s->anim_active && !s->anim_paused) return TRUE;
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
    int n = s->anim_active ? 1 : 0;
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
        s->anim_start_us = base_us;
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
    case NS_CSS_ANIMATION:
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
    if (ch->paused) return ch->paused_elapsed_ms;
    return (now_us - ch->start_us) / 1000.0 - ch->delay_ms;
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
}

static void
observe_transition_prop(ns_anim *a, ns_anim_state *s, const ns_style *style,
                        int prop, const ns_css_anim_entry *e, gint64 now_us)
{
    const ns_css_value *cur = style->values[prop];
    ns_anim_chan *ch = chan_ensure(s, prop);
    if (cur && ch->current && cur == ch->current) return;
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
        if (interpolable) {
            chan_start(a, s, ch, from, cur, e, now_us, FALSE);
        } else if (prop_discretely_animatable(prop)) {
            chan_start(a, s, ch, from, cur, e, now_us, TRUE);
        } else if (ch->active) {
            anim_emit(a, s->node, "transitioncancel",
                      ns_css_prop_name(ch->prop),
                      MAX(chan_elapsed_ms(ch, now_us), 0.0));
            ch->active = FALSE;
            if (a->active_count > 0) a->active_count--;
        }
    } else if (ch->active) {
        anim_emit(a, s->node, "transitioncancel", ns_css_prop_name(ch->prop),
                  MAX(chan_elapsed_ms(ch, now_us), 0.0));
        ch->active = FALSE;
        if (a->active_count > 0) a->active_count--;
    }
    ns_css_value_free(ch->last);
    ch->last = ns_css_value_dup(cur);
}

static void
observe_transition(ns_anim *a, ns_anim_state *s, const ns_style *style,
                   gint64 now_us)
{
    const ns_css_value *tv = style ? style->values[NS_CSS_TRANSITION] : NULL;
    gboolean has_list = tv && tv->kind == NS_CSS_V_ANIM && tv->u.anim.n > 0;
    s->has_transition = has_list;
    const ns_css_anim_entry *by_prop[NS_CSS_PROP_COUNT];
    memset(by_prop, 0, sizeof by_prop);
    gboolean touched[NS_CSS_PROP_COUNT];
    memset(touched, 0, sizeof touched);
    GArray *order = g_array_new(FALSE, FALSE, sizeof(int));
    if (has_list) {
        for (int i = 0; i < tv->u.anim.n; i++) {
            const ns_css_anim_entry *e = &tv->u.anim.entries[i];
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

static void
observe_animation(ns_anim *a, ns_anim_state *s, const ns_style *style,
                  gint64 now_us)
{
    const ns_css_value *av = style ? style->values[NS_CSS_ANIMATION] : NULL;
    if (!av || av->kind != NS_CSS_V_ANIM || av->u.anim.n == 0 ||
        !av->u.anim.entries[0].name || av->u.anim.entries[0].duration_ms < 0) {
        if (s->anim_active) {
            s->anim_active = FALSE;
            if (a->active_count > 0) a->active_count--;
        }
        if (s->anim_name) {
            anim_emit(a, s->node, "animationcancel", s->anim_name, 0.0);
            g_free(s->anim_name);
            s->anim_name = NULL;
        }
        if (s->anim_values) g_hash_table_remove_all(s->anim_values);
        return;
    }
    if (should_skip_motion()) return;
    const ns_css_anim_entry *e = &av->u.anim.entries[0];
    gboolean paused = e->paused;
    const ns_css_value *ps =
        style ? style->values[NS_CSS_ANIMATION_PLAY_STATE] : NULL;
    if (ps && ps->kind == NS_CSS_V_KEYWORD && ps->u.keyword) {
        if (strcmp(ps->u.keyword, "paused") == 0)       paused = TRUE;
        else if (strcmp(ps->u.keyword, "running") == 0) paused = FALSE;
    }
    if (s->anim_name && strcmp(s->anim_name, e->name) == 0 &&
        s->anim_duration_ms == e->duration_ms) {
        if (paused && !s->anim_paused) {
            double el = (now_us - s->anim_start_us) / 1000.0 - s->anim_delay_ms;
            s->anim_elapsed_base_ms = el > 0 ? el : 0;
            s->anim_paused = TRUE;
        } else if (!paused && s->anim_paused) {
            s->anim_start_us = now_us -
                (gint64)((s->anim_elapsed_base_ms + s->anim_delay_ms) * 1000.0);
            s->anim_paused = FALSE;
        }
        return;
    }
    if (!s->anim_active) {
        if (a->active_count >= NS_ANIM_MAX_ACTIVE) return;
        a->active_count++;
    }
    g_free(s->anim_name);
    s->anim_name = g_strdup(e->name);
    s->anim_start_us = now_us;
    s->anim_duration_ms = e->duration_ms;
    s->anim_delay_ms = e->delay_ms;
    s->anim_iter_count = e->iter_count;
    s->anim_direction = e->direction;
    s->anim_fill = e->fill;
    s->anim_timing = e->timing;
    s->anim_active = TRUE;
    s->anim_paused = paused;
    s->anim_started = FALSE;
    s->anim_finished = FALSE;
    s->anim_iters_emitted = 0;
    s->anim_elapsed_base_ms = 0;
    s->anim_generation++;
    ns_css_keyframes_resolved_free(s->anim_kf);
    anim_stops_free(s->anim_stops);
    const ns_css_keyframes *gkf = g_hash_table_lookup(a->keyframes, e->name);
    s->anim_kf = gkf ? ns_css_keyframes_resolve(gkf, style->vars) : NULL;
    s->anim_stops = anim_stops_build(s->anim_kf ? s->anim_kf : gkf);
    if (s->anim_values) g_hash_table_remove_all(s->anim_values);
    if (paused) advance_animation(a, s, now_us);
}

void
ns_anim_observe(ns_anim *a, const ns_node *dom,
                const ns_style *style, gint64 now_us)
{
    if (!a || !dom || !style) return;
    ns_anim_state *s = g_hash_table_lookup(a->states, dom);
    if (!s) {
        const ns_css_value *tv = style->values[NS_CSS_TRANSITION];
        const ns_css_value *av = style->values[NS_CSS_ANIMATION];
        gboolean animatable =
            (tv && tv->kind == NS_CSS_V_ANIM && tv->u.anim.n > 0) ||
            (av && av->kind == NS_CSS_V_ANIM && av->u.anim.n > 0);
        if (!animatable)
            return;
        s = state_for(a, dom);
    }
    observe_transition(a, s, style, now_us);
    observe_animation(a, s, style, now_us);
    anim_track(a, s);
}

static void
style_set_value(ns_style *st, int prop, ns_css_value *v)
{
    if (!st || prop < 0 || prop >= NS_CSS_PROP_COUNT || !v) return;
    if (st->values[prop] == v) return;
    ns_css_value_free(st->values[prop]);
    st->values[prop] = ns_css_value_dup(v);
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
                    style_set_value(st, ch->prop, ch->current);
            }
        if (s->anim_values) {
            GHashTableIter vit;
            gpointer vk, vv;
            g_hash_table_iter_init(&vit, s->anim_values);
            while (g_hash_table_iter_next(&vit, &vk, &vv))
                style_set_value(st, GPOINTER_TO_INT(vk), vv);
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
anim_sample_at(ns_anim_state *s, double progress)
{
    double pct = timing_apply(s->anim_timing, progress) * 100.0;
    if (!s->anim_values)
        s->anim_values = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                               NULL, (GDestroyNotify)ns_css_value_free);
    g_hash_table_remove_all(s->anim_values);
    if (!s->anim_stops) return;
    gboolean seen[NS_CSS_PROP_COUNT];
    memset(seen, 0, sizeof seen);
    for (guint i = 0; i < s->anim_stops->len; i++) {
        const ns_anim_kf_stop *st = &g_array_index(s->anim_stops, ns_anim_kf_stop, i);
        if (!st->decls) continue;
        for (guint d = 0; d < st->decls->len; d++) {
            int prop = (int)g_array_index(st->decls, ns_css_decl, d).prop;
            if (prop < 0 || prop >= NS_CSS_PROP_COUNT || seen[prop]) continue;
            seen[prop] = TRUE;
            const ns_anim_kf_stop *prev = NULL, *next = NULL;
            for (guint k = 0; k < s->anim_stops->len; k++) {
                const ns_anim_kf_stop *c = &g_array_index(s->anim_stops, ns_anim_kf_stop, k);
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
            if (out) g_hash_table_insert(s->anim_values, GINT_TO_POINTER(prop), out);
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

static double
anim_elapsed_ms(const ns_anim_state *s, gint64 now_us)
{
    return s->anim_paused
        ? s->anim_elapsed_base_ms
        : (now_us - s->anim_start_us) / 1000.0 - s->anim_delay_ms;
}

static gboolean
advance_animation(ns_anim *a, ns_anim_state *s, gint64 now_us)
{
    if (!s->anim_name) return FALSE;
    double cycle_ms = s->anim_duration_ms > 0 ? s->anim_duration_ms : 1.0;
    double elapsed = anim_elapsed_ms(s, now_us);
    if (elapsed < 0) {
        gboolean fill_back = s->anim_fill == NS_CSS_ANIM_FILL_BACKWARDS ||
                             s->anim_fill == NS_CSS_ANIM_FILL_BOTH;
        if (!fill_back) {
            if (s->anim_values) g_hash_table_remove_all(s->anim_values);
            return FALSE;
        }
        anim_sample_at(s, directed_progress(0, 0.0, s->anim_direction));
        return TRUE;
    }
    double iter_d = elapsed / cycle_ms;
    if (iter_d > 1e9) iter_d = 1e9;
    int iter = (int)iter_d;
    if (s->anim_iter_count > 0 && iter >= s->anim_iter_count) {
        if (s->anim_active) {
            s->anim_active = FALSE;
            s->anim_finished = TRUE;
            if (a->active_count > 0) a->active_count--;
        }
        gboolean fill_fwd = s->anim_fill == NS_CSS_ANIM_FILL_FORWARDS ||
                            s->anim_fill == NS_CSS_ANIM_FILL_BOTH;
        if (!fill_fwd) {
            if (s->anim_values) g_hash_table_remove_all(s->anim_values);
            return TRUE;
        }
        int last = s->anim_iter_count - 1;
        anim_sample_at(s, directed_progress(last, 1.0, s->anim_direction));
        return TRUE;
    }
    double raw = fmod(elapsed, cycle_ms) / cycle_ms;
    anim_sample_at(s, directed_progress(iter, raw, s->anim_direction));
    return TRUE;
}

gboolean
ns_anim_tick(ns_anim *a, gint64 now_us)
{
    if (!a) return FALSE;
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
        gboolean an0 = s->anim_active;
        if (s->anim_active && advance_animation(a, s, now_us)) any = TRUE;
        if (an0) {
            double el = anim_elapsed_ms(s, now_us);
            if (!s->anim_started && el >= 0) {
                s->anim_started = TRUE;
                s->anim_iters_emitted = 0;
                anim_emit(a, s->node, "animationstart", s->anim_name, 0.0);
            }
            if (s->anim_started && s->anim_duration_ms > 0) {
                int reached = (int)(el / s->anim_duration_ms);
                if (s->anim_iter_count > 0 && reached > s->anim_iter_count - 1)
                    reached = s->anim_iter_count - 1;
                while (s->anim_iters_emitted < reached) {
                    s->anim_iters_emitted++;
                    anim_emit(a, s->node, "animationiteration", s->anim_name,
                              s->anim_iters_emitted * s->anim_duration_ms);
                }
            }
            if (!s->anim_active) {
                anim_emit(a, s->node, "animationend", s->anim_name,
                          s->anim_duration_ms *
                          (s->anim_iter_count > 0 ? s->anim_iter_count : 1));
                s->anim_started = FALSE;
                s->anim_iters_emitted = 0;
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
        if (s->anim_values) {
            GHashTableIter vit;
            gpointer vk, vv;
            g_hash_table_iter_init(&vit, s->anim_values);
            while (g_hash_table_iter_next(&vit, &vk, &vv))
                if (ns_css_prop_affects_layout(GPOINTER_TO_INT(vk))) return TRUE;
        }
    }
    return FALSE;
}

static const ns_css_value *
state_prop_value(const ns_anim_state *s, int prop)
{
    if (s->anim_values) {
        const ns_css_value *v = g_hash_table_lookup(s->anim_values,
                                                    GINT_TO_POINTER(prop));
        if (v) return v;
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
chan_info(const ns_anim_state *s, const ns_anim_chan *ch, gint64 now_us,
          ns_anim_info *out)
{
    memset(out, 0, sizeof *out);
    out->node = s->node;
    out->prop = ch->prop;
    out->name = ns_css_prop_name(ch->prop);
    out->duration_ms = ch->duration_ms;
    out->delay_ms = ch->delay_ms;
    out->iterations = 1;
    out->active = ch->active;
    out->paused = ch->paused;
    out->finished = ch->finished;
    out->generation = ch->generation;
    double el = chan_elapsed_ms(ch, now_us) + ch->delay_ms;
    if (ch->finished) el = ch->delay_ms + ch->duration_ms;
    out->current_ms = MAX(el, 0.0);
}

static void
anim_info(const ns_anim_state *s, gint64 now_us, ns_anim_info *out)
{
    memset(out, 0, sizeof *out);
    out->node = s->node;
    out->prop = NS_ANIM_KEYFRAME_PROP;
    out->name = s->anim_name;
    out->duration_ms = s->anim_duration_ms;
    out->delay_ms = s->anim_delay_ms;
    out->iterations = s->anim_iter_count > 0 ? s->anim_iter_count : INFINITY;
    out->active = s->anim_active;
    out->paused = s->anim_paused;
    out->finished = s->anim_finished;
    out->generation = s->anim_generation;
    double el = anim_elapsed_ms(s, now_us) + s->anim_delay_ms;
    if (s->anim_finished)
        el = s->anim_delay_ms + s->anim_duration_ms * out->iterations;
    out->current_ms = MAX(el, 0.0);
}

void
ns_anim_visit(ns_anim *a, const ns_node *node, ns_anim_visit_cb cb,
              gpointer user)
{
    if (!a || !cb) return;
    gint64 now = g_get_monotonic_time();
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, a->states);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_anim_state *s = val;
        if (node && s->node != node) continue;
        ns_anim_info info;
        if (s->anim_name && (s->anim_active || s->anim_finished)) {
            anim_info(s, now, &info);
            cb(&info, user);
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

gboolean
ns_anim_info_for(ns_anim *a, const ns_node *node, int prop, ns_anim_info *out)
{
    if (!a || !node || !out) return FALSE;
    ns_anim_state *s = g_hash_table_lookup(a->states, node);
    if (!s) return FALSE;
    gint64 now = g_get_monotonic_time();
    if (prop == NS_ANIM_KEYFRAME_PROP) {
        if (!s->anim_name) return FALSE;
        anim_info(s, now, out);
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
    gint64 now = g_get_monotonic_time();
    if (prop == NS_ANIM_KEYFRAME_PROP) {
        if (!s->anim_name) return FALSE;
        double el = ms - s->anim_delay_ms;
        if (s->anim_paused) s->anim_elapsed_base_ms = MAX(el, 0.0);
        else s->anim_start_us = now - (gint64)(ms * 1000.0);
        if (!s->anim_active && s->anim_finished) {
            s->anim_active = TRUE;
            s->anim_finished = FALSE;
            a->active_count++;
        }
        advance_animation(a, s, now);
        anim_track(a, s);
        return TRUE;
    }
    ns_anim_chan *ch = chan_find(s, prop);
    if (!ch || (!ch->active && !ch->finished)) return FALSE;
    double el = ms - ch->delay_ms;
    if (ch->paused) ch->paused_elapsed_ms = el;
    else ch->start_us = now - (gint64)(ms * 1000.0);
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
    gint64 now = g_get_monotonic_time();
    if (prop == NS_ANIM_KEYFRAME_PROP) {
        if (!s->anim_name) return FALSE;
        if (strcmp(op, "pause") == 0 && !s->anim_paused) {
            s->anim_elapsed_base_ms = MAX(anim_elapsed_ms(s, now), 0.0);
            s->anim_paused = TRUE;
        } else if (strcmp(op, "play") == 0 && s->anim_paused) {
            s->anim_start_us = now -
                (gint64)((s->anim_elapsed_base_ms + s->anim_delay_ms) * 1000.0);
            s->anim_paused = FALSE;
        } else if (strcmp(op, "finish") == 0) {
            double total = s->anim_delay_ms + s->anim_duration_ms *
                           (s->anim_iter_count > 0 ? s->anim_iter_count : 1);
            s->anim_paused = FALSE;
            s->anim_start_us = now - (gint64)(total * 1000.0);
            advance_animation(a, s, now);
        } else if (strcmp(op, "cancel") == 0) {
            if (s->anim_active && a->active_count > 0) a->active_count--;
            s->anim_active = FALSE;
            s->anim_finished = FALSE;
            anim_emit(a, s->node, "animationcancel", s->anim_name, 0.0);
            g_free(s->anim_name);
            s->anim_name = NULL;
            if (s->anim_values) g_hash_table_remove_all(s->anim_values);
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
        ch->start_us = now - (gint64)((ch->paused_elapsed_ms + ch->delay_ms) * 1000.0);
        ch->paused = FALSE;
    } else if (strcmp(op, "finish") == 0) {
        ch->paused = FALSE;
        ch->start_us = now - (gint64)((ch->delay_ms + ch->duration_ms) * 1000.0);
        if (ch->active) advance_chan(a, s, ch, now);
    } else if (strcmp(op, "cancel") == 0) {
        if (ch->active) {
            anim_emit(a, s->node, "transitioncancel", ns_css_prop_name(ch->prop),
                      MAX(chan_elapsed_ms(ch, now), 0.0));
            if (a->active_count > 0) a->active_count--;
        }
        ch->active = FALSE;
        ch->finished = FALSE;
    }
    anim_track(a, s);
    return TRUE;
}
