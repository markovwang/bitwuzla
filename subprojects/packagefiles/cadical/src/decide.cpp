#include "internal.hpp"

namespace CaDiCaL {

static uint64_t mix_decision_priority (uint64_t x) {
  x += UINT64_C (0x9e3779b97f4a7c15);
  x = (x ^ (x >> 30)) * UINT64_C (0xbf58476d1ce4e5b9);
  x = (x ^ (x >> 27)) * UINT64_C (0x94d049bb133111eb);
  return x ^ (x >> 31);
}

static uint64_t decision_priority_key (uint32_t seed, int idx,
                                       uint32_t priority) {
  uint64_t x = seed;
  x <<= 32;
  x ^= (uint64_t) idx * UINT64_C (0xd6e8feb86659fd93);
  x ^= (uint64_t) priority * UINT64_C (0xa0761d6478bd642f);
  return mix_decision_priority (x);
}

void Internal::set_decision_priority_seed (uint32_t seed) {
  if (decision_priority_seed == seed)
    return;
  decision_priority_seed = seed;
  decision_priority_stats.seed = seed;
  decision_priority_candidates.clear ();
  for (int idx = 1; idx <= max_var; ++idx) {
    if (idx >= (int) decision_priority_entries.size ())
      break;
    DecisionPriorityEntry &entry = decision_priority_entries[idx];
    if (!entry.registered)
      continue;
    entry.key = decision_priority_key (decision_priority_seed, idx,
                                       entry.priority);
    entry.phase =
        (mix_decision_priority (entry.key ^ UINT64_C (0xe7037ed1a0b428db)) &
         1)
            ? 1
            : -1;
    entry.generation = ++decision_priority_generation;
    entry.queued = false;
    enqueue_decision_priority (idx);
  }
}

void Internal::clear_decision_priority () {
  for (int idx = 1; idx < (int) decision_priority_entries.size (); ++idx)
    if (decision_priority_entries[idx].registered)
      melt (idx);
  decision_priority_candidates.clear ();
  decision_priority_bucket_sizes.clear ();
  decision_priority_entries.clear ();
  decision_priority_entries.resize (vsize);
}

DecisionPriorityCandidate
Internal::decision_priority_candidate (int idx) const {
  assert (0 < idx);
  assert (idx < (int) decision_priority_entries.size ());
  const DecisionPriorityEntry &entry = decision_priority_entries[idx];
  DecisionPriorityCandidate candidate;
  candidate.priority = entry.priority;
  candidate.key = entry.key;
  candidate.idx = idx;
  candidate.generation = entry.generation;
  return candidate;
}

void Internal::enqueue_decision_priority (int idx) {
  if (idx <= 0 || idx >= (int) decision_priority_entries.size ())
    return;
  DecisionPriorityEntry &entry = decision_priority_entries[idx];
  if (!entry.registered || entry.queued || val (idx) || !active (idx))
    return;
  decision_priority_candidates.insert (decision_priority_candidate (idx));
  entry.queued = true;
}

void Internal::dequeue_decision_priority (int idx) {
  if (idx <= 0 || idx >= (int) decision_priority_entries.size ())
    return;
  DecisionPriorityEntry &entry = decision_priority_entries[idx];
  if (!entry.registered || !entry.queued)
    return;
  decision_priority_candidates.erase (decision_priority_candidate (idx));
  entry.queued = false;
}

void Internal::refresh_decision_priority (int idx) {
  dequeue_decision_priority (idx);
  enqueue_decision_priority (idx);
}

void Internal::add_decision_priority_lit (int lit, uint32_t priority) {
  assert (lit);
  const int idx = abs (lit);
  assert (idx <= max_var);
  if (idx >= (int) decision_priority_entries.size ())
    decision_priority_entries.resize ((size_t) idx + 1);

  decision_priority_stats.add_lit_calls++;
  DecisionPriorityEntry &entry = decision_priority_entries[idx];
  if (entry.registered && priority >= entry.priority) {
    decision_priority_stats.add_lit_duplicates++;
    return;
  }

  if (entry.registered) {
    dequeue_decision_priority (idx);
    auto old = decision_priority_bucket_sizes.find (entry.priority);
    assert (old != decision_priority_bucket_sizes.end ());
    if (!--old->second)
      decision_priority_bucket_sizes.erase (old);
  } else {
    freeze (idx);
    decision_priority_stats.unique_lits++;
  }

  decision_priority_bucket_sizes[priority]++;
  decision_priority_stats.max_priority_buckets =
      max<uint64_t> (decision_priority_stats.max_priority_buckets,
                     decision_priority_bucket_sizes.size ());

  entry.registered = true;
  entry.priority = priority;
  entry.key = decision_priority_key (decision_priority_seed, idx, priority);
  entry.phase =
      (mix_decision_priority (entry.key ^ UINT64_C (0xe7037ed1a0b428db)) & 1)
          ? 1
          : -1;
  entry.generation = ++decision_priority_generation;
  entry.queued = false;
  enqueue_decision_priority (idx);
}

int Internal::next_decision_priority_variable () {
  decision_priority_stats.decide_calls++;
  while (!decision_priority_candidates.empty ()) {
    const auto it = decision_priority_candidates.begin ();
    const DecisionPriorityCandidate candidate = *it;
    const int idx = candidate.idx;
    if (idx <= 0 || idx >= (int) decision_priority_entries.size ()) {
      decision_priority_candidates.erase (it);
      continue;
    }

    DecisionPriorityEntry &entry = decision_priority_entries[idx];
    const bool stale = !entry.registered || !entry.queued ||
                       entry.priority != candidate.priority ||
                       entry.key != candidate.key ||
                       entry.generation != candidate.generation;
    if (stale) {
      decision_priority_candidates.erase (it);
      continue;
    }
    if (val (idx) || !active (idx)) {
      decision_priority_candidates.erase (it);
      entry.queued = false;
      continue;
    }
    decision_priority_stats.decide_returns++;
    return idx;
  }
  decision_priority_stats.decide_fallbacks++;
  return 0;
}

int Internal::decision_priority_phase (int idx) {
  assert (0 < idx);
  assert (idx < (int) decision_priority_entries.size ());
  const DecisionPriorityEntry &entry = decision_priority_entries[idx];
  assert (entry.registered);
  if (entry.phase > 0) {
    decision_priority_stats.decide_positive++;
    return idx;
  }
  decision_priority_stats.decide_negative++;
  return -idx;
}

// This function determines the next decision variable on the queue, without
// actually removing it from the decision queue, e.g., calling it multiple
// times without any assignment will return the same result.  This is of
// course used below in 'decide' but also in 'reuse_trail' to determine the
// largest decision level to backtrack to during 'restart' without changing
// the assigned variables (if 'opts.restartreusetrail' is non-zero).

int Internal::next_decision_variable_on_queue () {
  int64_t searched = 0;
  int res = queue.unassigned;
  while (val (res))
    res = link (res).prev, searched++;
  if (searched) {
    stats.searched += searched;
    update_queue_unassigned (res);
  }
  LOG ("next queue decision variable %d bumped %" PRId64 "", res,
       bumped (res));
  return res;
}

// This function determines the best decision with respect to score.
//
int Internal::next_decision_variable_with_best_score () {
  int res = 0;
  for (;;) {
    res = scores.front ();
    if (!val (res))
      break;
    (void) scores.pop_front ();
  }
  LOG ("next decision variable %d with score %g", res, score (res));
  return res;
}

int Internal::next_decision_variable () {
  if (use_scores ())
    return next_decision_variable_with_best_score ();
  else
    return next_decision_variable_on_queue ();
}

/*------------------------------------------------------------------------*/

// Implements phase saving as well using a target phase during
// stabilization unless decision phase is forced to the initial value
// of a phase is forced through the 'phase' option.

int Internal::decide_phase (int idx, bool target) {
  const int initial_phase = opts.phase ? 1 : -1;
  int phase = 0;
  if (force_saved_phase)
    phase = phases.saved[idx];
  if (!phase)
    phase = phases.forced[idx]; // swapped with opts.forcephase case!
  if (!phase && opts.forcephase)
    phase = initial_phase;
  if (!phase && target)
    phase = phases.target[idx];
  if (!phase)
    phase = phases.saved[idx];

  // The following should not be necessary and in some version we had even
  // a hard 'COVER' assertion here to check for this.   Unfortunately it
  // triggered for some users and we could not get to the root cause of
  // 'phase' still not being set here.  The logic for phase and target
  // saving is pretty complex, particularly in combination with local
  // search, and to avoid running in such an issue in the future again, we
  // now use this 'defensive' code here, even though such defensive code is
  // considered bad programming practice.
  //
  if (!phase)
    phase = initial_phase;

  return phase * idx;
}

// The likely phase of an variable used in 'collect' for optimizing
// co-location of clauses likely accessed together during search.

int Internal::likely_phase (int idx) { return decide_phase (idx, false); }

/*------------------------------------------------------------------------*/

bool Internal::satisfied () {
  size_t assigned = trail.size ();
  if (propagated < assigned)
    return false;
  if ((size_t) level < assumptions.size () + (!!constraint.size ()))
    return false;
  return (assigned == (size_t) max_var);
}

bool Internal::better_decision (int lit, int other) {
  int lit_idx = abs (lit);
  int other_idx = abs (other);
  if (stable)
    return stab[lit_idx] > stab[other_idx];
  else
    return btab[lit_idx] > btab[other_idx];
}

// Search for the next decision and assign it to the saved phase.  Requires
// that not all variables are assigned.

int Internal::decide () {
  assert (!satisfied ());
  START (decide);
  int res = 0;
  if ((size_t) level < assumptions.size ()) {
    const int lit = assumptions[level];
    assert (assumed (lit));
    const signed char tmp = val (lit);
    if (tmp < 0) {
      LOG ("assumption %d falsified", lit);
      res = 20;
    } else if (tmp > 0) {
      LOG ("assumption %d already satisfied", lit);
      level++;
      control.push_back (Level (0, trail.size ()));
      LOG ("added pseudo decision level");
      notify_decision ();
    } else {
      LOG ("deciding assumption %d", lit);
      search_assume_decision (lit);
    }
  } else if ((size_t) level == assumptions.size () && constraint.size ()) {

    int satisfied_lit = 0;  // The literal satisfying the constrain.
    int unassigned_lit = 0; // Highest score unassigned literal.
    int previous_lit = 0;   // Move satisfied literals to the front.

    const size_t size_constraint = constraint.size ();

#ifndef NDEBUG
    unsigned sum = 0;
    for (auto lit : constraint)
      sum += lit;
#endif
    for (size_t i = 0; i != size_constraint; i++) {

      // Get literal and move 'constraint[i] = constraint[i-1]'.

      int lit = constraint[i];
      constraint[i] = previous_lit;
      previous_lit = lit;

      const signed char tmp = val (lit);
      if (tmp < 0) {
        LOG ("constraint literal %d falsified", lit);
        continue;
      }

      if (tmp > 0) {
        LOG ("constraint literal %d satisfied", lit);
        satisfied_lit = lit;
        break;
      }

      assert (!tmp);
      LOG ("constraint literal %d unassigned", lit);

      if (!unassigned_lit || better_decision (lit, unassigned_lit))
        unassigned_lit = lit;
    }

    if (satisfied_lit) {

      constraint[0] = satisfied_lit; // Move satisfied to the front.

      LOG ("literal %d satisfies constraint and "
           "is implied by assumptions",
           satisfied_lit);

      level++;
      control.push_back (Level (0, trail.size ()));
      LOG ("added pseudo decision level for constraint");
      notify_decision ();

    } else {

      // Just move all the literals back.  If we found an unsatisfied
      // literal then it will be satisfied (most likely) at the next
      // decision and moved then to the first position.

      if (size_constraint) {

        for (size_t i = 0; i + 1 != size_constraint; i++)
          constraint[i] = constraint[i + 1];

        constraint[size_constraint - 1] = previous_lit;
      }

      if (unassigned_lit) {

        LOG ("deciding %d to satisfy constraint", unassigned_lit);
        search_assume_decision (unassigned_lit);

      } else {

        LOG ("failing constraint");
        unsat_constraint = true;
        res = 20;
      }
    }

#ifndef NDEBUG
    for (auto lit : constraint)
      sum -= lit;
    assert (!sum); // Checksum of literal should not change!
#endif

  } else {
    stats.decisions++;
    int decision = ask_decision ();
    if (!decision) {
      int idx = next_decision_priority_variable ();
      if (idx) {
        decision = decision_priority_phase (idx);
        LOG ("native decision priority picks %d", decision);
      } else {
        idx = next_decision_variable ();
        const bool target = (opts.target > 1 || (stable && opts.target));
        decision = decide_phase (idx, target);
      }
    }
    search_assume_decision (decision);
  }
  if (res)
    marked_failed = false;
  STOP (decide);
  return res;
}

} // namespace CaDiCaL
