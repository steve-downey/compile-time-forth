# compile-time-forth checklist

## Forth compiler (docs/forth-plan.md — superseded R1 plan)

- [x] Step F0: governance install
- [x] Step F1: C++26 baseline
- [x] Step F2: vendor Beman Execution
- [x] Step F3: import foundation
- [x] Step F4: import parser combinators
- [x] Step F5: Forth lexical layer
- [x] Step F6: syntax tree
- [x] Step F7: grammar
- [x] Step F8: machine substrate
- [x] Step F9: dictionary
- [x] Step F10: data space
- [x] Step F11: elaborated core and resolution
- [x] Step F12: stack-effect analysis
- [x] Step F13: direct evaluator
- [x] Step F14: stack-machine codegen and VM
- [x] Step F15: public one-shot API
- [x] Step F16: memory words end-to-end
- [x] Step F17: counted loops
- [~] Step F18a: execution tokens and exceptions — retired unexecuted, see docs/forth-plan-2.md (F28, F31)
- [~] Step F18: sender/receiver CPS backend — retired unexecuted, see docs/forth-plan-2.md (F33)
- [~] Step F19: foreign function interface — retired unexecuted, see docs/forth-plan-2.md (F34)
- [~] Step F20: CREATE/DOES> (optional) — retired unexecuted, see docs/forth-plan-2.md (F28, now required)
- [~] Step F21: error-quality and negative-compile pass — retired unexecuted, see docs/forth-plan-2.md (F36)
- [~] Step F22: documentation consolidation — retired unexecuted, see docs/forth-plan-2.md (F36)

## True Forth revision (docs/forth-plan-2.md)

- [x] Step F23: revision governance and pivot record
- [x] Step F24: interpreter, interpret state only
- [ ] Step F25: colon compiler and session image
- [ ] Step F26: the cut
- [ ] Step F27: immediacy and control flow
- [ ] Step F28: execution tokens and defining words
- [ ] Step F29: parsing words and strings
- [ ] Step F30: effect lint
- [ ] Step F31: CATCH and THROW
- [ ] Step F32: conformance
- [ ] Step F33: sender backend
- [ ] Step F34: foreign function interface
- [ ] Step F35: bootstrap prelude (stretch)
- [ ] Step F36: consolidation

## Blog series (docs/blog/AGENTS.md — distinct Sonnet blog agent, one post per step)

- [x] Blog: F0–F15 arc (Parts 0-9)
- [x] Blog: F16 memory words end-to-end (Part 10)
- [x] Blog: F17 counted loops (Part 11)

## Blog series continuation (one post per step; F23 = Part 12)

- [ ] Blog: F23 the pivot (Part 12)
- [ ] Blog: F24 (Part 13) … F36 (Part 25), one per step as merged
