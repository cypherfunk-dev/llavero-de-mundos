#include "talk.h"

#include <string.h>

#include "director.h"
#include "habits.h"
#include "timeutil.h"

/* Días hasta que se retoma una respuesta (0 = no se retoma) y entre dos preguntas iguales. */
static const uint8_t FOLLOWUP[Q_COUNT] = {
    [Q_PLANTAS] = 3, [Q_ALMA] = 4, [Q_MADRUGAR] = 6, [Q_MASCOTA] = 5,
};
#define Q_REPEAT_DAYS 20

int talk_followup_days(int q)
{
    return q >= 0 && q < Q_COUNT ? FOLLOWUP[q] : 0;
}

static uint16_t today(const GameState *gs)
{
    return (uint16_t)gs->day_key;
}

static void note(GameState *gs, int spk, int amount)
{
    int v = gs->sala.affinity[spk] + amount;
    gs->sala.affinity[spk] = (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v);
}

/* Pone una frase en la caja: guarda cuál es, quién habla y los huecos; apunta la pregunta o la
 * escena en curso, anota que el suceso comentado ya no es nuevo y avisa al observador. */
static void show(GameState *gs, const PhraseCtx *c, int idx)
{
    PhraseRec r;
    if (!phrase_get(idx, &r))
        return;
    Sala *s = &gs->sala;
    PhraseFill f;
    phrase_fill_prepare(gs, c, idx, &f, &gs->rng);
    s->phrase = (uint16_t)(idx + 1);
    s->who = r.speaker;
    memcpy(s->fill, f.v, sizeof(s->fill));
    s->stage = r.kind == PK_PREGUNTA ? DLG_ASK : DLG_SAY;
    s->q = r.kind == PK_PREGUNTA ? (uint8_t)(r.karg + 1) : 0;
    s->scene = 0;
    s->scene_line = 0;
    if (r.kind == PK_ESCENA) {
        s->scene_line = r.kn;
        if (phrase_scene_find(r.karg, r.kn + 1) >= 0)
            s->scene = r.karg; /* aún quedan líneas */
    }
    if (r.kind == PK_PREGUNTA && r.karg < LLV_Q_MAX)
        s->memo_day[r.karg] = today(gs);
    for (int i = 0; i < r.ntags; i++)
        if (phrase_tag_kind(&r, i) == PT_EVENT)
            state_fresh_clear(gs, (EventKind)phrase_event_kind(phrase_tag_arg(&r, i)));
    char buf[256];
    phrase_render(gs, idx, &f, buf, sizeof(buf));
    phrase_notify(r.speaker, r.kind, r.picaro, buf);
}

static PhraseQuery query(uint32_t kinds, uint32_t speakers, int karg)
{
    PhraseQuery q;
    memset(&q, 0, sizeof(q));
    q.kinds = kinds;
    q.speakers = speakers;
    q.karg = karg;
    q.remember = true;
    return q;
}

bool talk_say(GameState *gs, const PhraseEnv *env, uint32_t kinds, uint32_t speakers, int karg)
{
    PhraseCtx c;
    phrase_ctx_make(&c, gs, env);
    PhraseQuery q = query(kinds, speakers & c.present ? speakers & c.present : speakers, karg);
    int idx = phrase_pick(&c, gs, &q);
    if (idx < 0)
        return false;
    show(gs, &c, idx);
    return true;
}

bool talk_comment(GameState *gs, const PhraseEnv *env)
{
    PhraseCtx c;
    phrase_ctx_make(&c, gs, env);
    PhraseQuery q = query(PK_BIT(PK_COMENTARIO), c.present, -1);
    int idx = phrase_pick(&c, gs, &q);
    if (idx < 0)
        return false;
    show(gs, &c, idx);
    return true;
}

void talk_visitor(GameState *gs, int spk, int ticks, bool log)
{
    gs->sala.visitor = (uint8_t)spk;
    gs->sala.visitor_left = (uint8_t)(ticks > 255 ? 255 : ticks);
    if (log)
        state_log(gs, EV_VISITOR, (uint8_t)spk);
}

void talk_gato(GameState *gs, const PhraseEnv *env)
{
    talk_visitor(gs, SPK_GATO, LLV_TICKS_PER_HOUR, true);
    talk_say(gs, env, PK_BIT(PK_CHARLA), SPK_BIT(SPK_GATO), -1);
}

bool talk_shake(GameState *gs, const PhraseEnv *env)
{
    if (gs->sala.stage == DLG_ASK)
        return false;
    PhraseEnv e = *env;
    e.shaken = true;
    PhraseCtx c;
    phrase_ctx_make(&c, gs, &e);
    PhraseQuery q = query(PK_BIT(PK_CHARLA), c.present, -1);
    q.need_tag = PT_SHAKEN;
    int idx = phrase_pick(&c, gs, &q);
    if (idx < 0)
        return false;
    show(gs, &c, idx);
    return true;
}

void talk_on_wake(GameState *gs, int64_t slept)
{
    if (slept >= 1800) {
        Sala *s = &gs->sala;
        s->stage = DLG_IDLE;
        s->phrase = 0;
        s->scene = s->scene_line = 0;
        s->q = 0;
    }
}

bool talk_text(const GameState *gs, char *buf, size_t n)
{
    buf[0] = '\0';
    const Sala *s = &gs->sala;
    if (s->stage == DLG_IDLE || s->phrase == 0)
        return false;
    PhraseFill f;
    memcpy(f.v, s->fill, sizeof(f.v));
    phrase_render(gs, s->phrase - 1, &f, buf, n);
    return buf[0] != '\0';
}

bool talk_picaro(const GameState *gs)
{
    PhraseRec r;
    return gs->sala.phrase && phrase_get(gs->sala.phrase - 1, &r) && r.picaro;
}

/* ---------- las distintas formas de empezar ---------- */

static TalkResult filler(GameState *gs, const PhraseEnv *env)
{
    PhraseCtx c;
    phrase_ctx_make(&c, gs, env);
    PhraseQuery q = query(PK_BIT(PK_RELLENO) | PK_BIT(PK_SINACCIONES), c.present, -1);
    int idx = phrase_pick(&c, gs, &q);
    if (idx >= 0)
        show(gs, &c, idx);
    return TALK_FILLER;
}

/* Una pregunta de hábito que toque ahora (spec §3.3: 1-2 al día, sin insistir). */
static bool try_habit(GameState *gs, const PhraseCtx *c, const PhraseEnv *env)
{
    int cand[HAB_WITH_QUESTION], n = 0;
    for (int q = 0; q < HAB_WITH_QUESTION; q++) {
        PhraseQuery pq = query(PK_BIT(PK_PREGUNTA), c->present, q);
        if (habit_question_ok(gs, q, env->now) && phrase_eligible(c, &pq) > 0)
            cand[n++] = q;
    }
    if (n == 0)
        return false;
    int q = cand[rng_below(&gs->rng, (uint32_t)n)];
    PhraseQuery pq = query(PK_BIT(PK_PREGUNTA), c->present, q);
    int idx = phrase_pick(c, gs, &pq);
    if (idx < 0)
        return false;
    habit_note_asked(gs, q, env->now);
    show(gs, c, idx);
    return true;
}

/* Una pregunta de las de siempre (sin hábito) que no se haya hecho hace poco. */
static bool try_plain(GameState *gs, const PhraseCtx *c)
{
    int cand[Q_COUNT], n = 0;
    for (int q = HAB_WITH_QUESTION; q < Q_COUNT; q++) {
        uint16_t last = gs->sala.memo_day[q];
        PhraseQuery pq = query(PK_BIT(PK_PREGUNTA), c->present, q);
        if ((last == 0 || (uint16_t)(today(gs) - last) >= Q_REPEAT_DAYS) && phrase_eligible(c, &pq) > 0)
            cand[n++] = q;
    }
    if (n == 0)
        return false;
    int q = cand[rng_below(&gs->rng, (uint32_t)n)];
    PhraseQuery pq = query(PK_BIT(PK_PREGUNTA), c->present, q);
    int idx = phrase_pick(c, gs, &pq);
    if (idx < 0)
        return false;
    show(gs, c, idx);
    return true;
}

/* Retoma una respuesta de hace días (spec §4.3: "¿Y esas plantas?"). */
static bool try_retoma(GameState *gs, const PhraseCtx *c)
{
    for (int q = 0; q < Q_COUNT; q++) {
        uint8_t m = gs->sala.memo[q];
        if (FOLLOWUP[q] == 0 || (m & 3) == 0 || (m & 4) ||
            (uint16_t)(today(gs) - gs->sala.memo_day[q]) < FOLLOWUP[q])
            continue;
        PhraseQuery pq = query(PK_BIT(PK_RETOMA), c->present, q);
        int idx = phrase_pick(c, gs, &pq);
        if (idx < 0)
            continue;
        gs->sala.memo[q] = (uint8_t)(m | 4);
        show(gs, c, idx);
        return true;
    }
    return false;
}

static bool try_kind(GameState *gs, const PhraseCtx *c, uint32_t kinds, bool scene)
{
    PhraseQuery q = query(kinds, c->present, -1);
    if (scene) {
        q.scene_line = 1;
        q.scene_start = true;
    }
    int idx = phrase_pick(c, gs, &q);
    if (idx < 0)
        return false;
    show(gs, c, idx);
    return true;
}

static TalkResult start_conversation(GameState *gs, const PhraseEnv *env)
{
    if (gs->actions_used[WORLD_SALA] >= LLV_SALA_ACTIONS)
        return filler(gs, env);
    gs->actions_used[WORLD_SALA]++;
    PhraseCtx c;
    phrase_ctx_make(&c, gs, env);
    uint32_t r = rng_below(&gs->rng, 100);
    if (!(try_retoma(gs, &c) || (r < 35 && try_habit(gs, &c, env)) || (r < 50 && try_plain(gs, &c)) ||
          (r < 65 && try_kind(gs, &c, PK_BIT(PK_ESCENA), true)) ||
          (r < 85 && try_kind(gs, &c, PK_BIT(PK_COMENTARIO), false)) ||
          try_kind(gs, &c, PK_BIT(PK_CHARLA), false)))
        return filler(gs, env);
    return TALK_STARTED;
}

static TalkResult answer(GameState *gs, const PhraseEnv *env, bool yes)
{
    Sala *s = &gs->sala;
    int q = s->q - 1, asker = s->who;
    s->q = 0;
    s->stage = DLG_SAY;
    s->memo[q] = (uint8_t)(yes ? 1 : 2);
    s->memo_day[q] = today(gs);
    int ha = q < HAB_WITH_QUESTION ? habit_answer(gs, q, yes) : 0;
    note(gs, asker, 3);

    PhraseCtx c;
    phrase_ctx_make(&c, gs, env);
    /* Se puede mentir: un sí que no cuadra, o a veces sin motivo, y el Robot "lo detecta". */
    bool lie = (ha & HA_LIE) != 0 ||
               (yes && q < HAB_WITH_QUESTION && rng_below(&gs->rng, 100) < LLV_LIE_PCT);
    if (lie) {
        PhraseQuery lq = query(PK_BIT(PK_MENTIRA), c.present & (SPK_BIT(SPK_ROBOT) | SPK_BIT(asker)), q);
        int idx = phrase_pick(&c, gs, &lq);
        if (idx >= 0) {
            show(gs, &c, idx);
            return TALK_ANSWERED;
        }
    }
    PhraseQuery rq = query(PK_BIT(yes ? PK_SI : PK_NO), SPK_BIT(asker), q);
    int idx = phrase_pick(&c, gs, &rq);
    if (idx < 0) {
        rq.speakers = c.present;
        idx = phrase_pick(&c, gs, &rq);
    }
    if (idx >= 0)
        show(gs, &c, idx);
    return TALK_ANSWERED;
}

TalkResult talk_tap(GameState *gs, const PhraseEnv *env)
{
    Sala *s = &gs->sala;
    if (s->stage == DLG_ASK && s->q)
        return answer(gs, env, true);
    if (s->stage == DLG_SAY && s->scene > 0) {
        int next = phrase_scene_find(s->scene, s->scene_line + 1);
        if (next >= 0) {
            PhraseCtx c;
            phrase_ctx_make(&c, gs, env);
            phrase_remember(gs, next);
            show(gs, &c, next);
            return TALK_ADVANCED;
        }
        s->scene = 0;
    }
    return start_conversation(gs, env);
}

TalkResult talk_hold(GameState *gs, const PhraseEnv *env)
{
    Sala *s = &gs->sala;
    if (s->stage == DLG_ASK && s->q)
        return answer(gs, env, false);
    if (gs->actions_used[WORLD_SALA] >= LLV_SALA_ACTIONS)
        return filler(gs, env);
    gs->actions_used[WORLD_SALA]++;
    /* Entra otro visitante (no el que ya está). */
    static const uint8_t GUESTS[3] = {SPK_GOTICA, SPK_GAMER, SPK_BOOMER};
    int pick = (int)rng_below(&gs->rng, 3);
    if (GUESTS[pick] == s->visitor)
        pick = (pick + 1) % 3;
    talk_visitor(gs, GUESTS[pick], LLV_VISIT_STAY_TICKS, true);
    PhraseCtx c;
    phrase_ctx_make(&c, gs, env);
    PhraseQuery q = query(PK_BIT(PK_TIMBRE), SPK_BIT(GUESTS[pick]), -1);
    int idx = phrase_pick(&c, gs, &q);
    if (idx >= 0)
        show(gs, &c, idx);
    return TALK_BELL;
}

/* ---------- el paso del tiempo en la Sala ---------- */

void sala_tick(GameState *gs)
{
    Sala *s = &gs->sala;
    CivilTime t = time_to_civil(gs->tick * LLV_TICK_SECONDS);

    /* Las fechas especiales traen a su visitante a media tarde (sin tirar dados). */
    int date = phrase_special_date(gs->tick * LLV_TICK_SECONDS);
    if (t.hour == 16 && t.minute == 0 && date != DATE_NONE) {
        static const uint8_t THEMED[6] = {0, SPK_GOTICA, SPK_GATO, SPK_BOOMER, SPK_GAMER, 0};
        if (THEMED[date])
            talk_visitor(gs, THEMED[date], 7 * LLV_TICKS_PER_HOUR, true);
    }
    if (s->visitor_left > 0) {
        if (--s->visitor_left == 0)
            s->visitor = 0;
    } else if (s->visitor != 0) {
        s->visitor = 0;
    } else if (rng_below(&gs->rng, 1000) < LLV_VISIT_PERMILLE) {
        static const uint8_t GUESTS[3] = {SPK_GOTICA, SPK_GAMER, SPK_BOOMER};
        talk_visitor(gs, GUESTS[rng_below(&gs->rng, 3)], LLV_VISIT_STAY_TICKS, true);
    }
    director_patience(gs);
}
