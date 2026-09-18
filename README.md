# Compile and Runtime FORTH in Contemporary C++

[![OpenSSF Baseline](https://www.bestpractices.dev/projects/12577/baseline)](https://www.bestpractices.dev/projects/12577)

`smd::forth` is a Forth-2012 text interpreter, implemented in C++26, that runs the same way at compile time (as a `constexpr` evaluation) and at ordinary runtime. `smd::forth::compiled_forth<"...">` takes a Forth program as a template argument, interprets it once during translation, and hands back a session image — code space, dictionary, and data space — as a trivially copyable literal a caller can re-run at runtime, inspect, or extend. A malformed program is a hard compile error, the same way a malformed C++ program is.

This began, per the repo's history, as a trivial "best practices" example project — a library that returned a name, a test, and a hello-world example — and that scaffolding is still what the build tooling below was proven out on. It has since grown into the actual thing: a working Forth implementation with its own colon compiler, execution tokens, `CATCH`/`THROW`, a stack-effect lint, a sender/receiver (Execution26) backend as a second executor of the same compiled code, a foreign-function interface, and a Forth-2012 core-word conformance battery differentially tested against `gforth`.

Start with [`docs/compiler_architecture.org`](docs/compiler_architecture.org) for the living architecture document (every code sample transcluded from the real source tree by UUID anchor), [`docs/forth-limitations.md`](docs/forth-limitations.md) for what this project deliberately does not implement and every recorded divergence from Forth-2012, [`docs/forth-plan-2.md`](docs/forth-plan-2.md) for the governing step-by-step plan, and [`compile-time-forth.org`](compile-time-forth.org) (`make presentation`) for a slide-shaped tour of the same system. `docs/blog/` carries a build-log series, one post per step.

The C++ src is all in the ./src directory, including the headers and tests. Take a look at [The Pitchfork Layout Spec](https://www.w3.org/publications/spec-generator/?type=bikeshed-spec&output=html&die-on=fatal&md-date=&url=https%3A%2F%2Fraw.githubusercontent.com%2Fvector-of-bool%2Fpitchfork%2Fdevelop%2Fdata%2Fspec.bs&file=) for some discussion about merged layouts. Short answer is that include directories are an install location, not a source location, but that the directory layouts must still be coherent. Tests are co-located because tests are important and the further away they are, the more they will be dropped.

The CMake is contemporary, post-modern, so not just target oriented, it is also file set oriented.

GitHub Actions are set up to make sure everything I expect to work actually does.

There is a top-level Makefile to drive workflow. Its default is to build and run all tests for the project.

The project levergages `uv` and PyPI to install the tools that it requires. It installs them into a local virtual environment so as not to make system wide changes.

The repository also vendors a generic WG21 paper framework under `papers/wg21` via `git subtree`. It is intentionally generic rather than tied to any one proposal number. Use `make papers` to build the vendored papers, `make clean` to remove their generated outputs, and `make realclean` to remove the paper tool infrastructure as well.

The [pre-commit](https://github.com/pre-commit/pre-commit) framework is used to drive linters both locally and in GitHub Actions. Clang format is enforced, as is a CMake format. I've given up doing this by hand. Yaml is even worse. Spellcheck, for code, also.

The infra directory is vendored in from the [Beman Project](https://github.com/bemanproject/infra) supporting [infra](https://github.com/bemanproject/infra) project. Right now for install of the project. Many of the GitHub actions in .github/workflows/ also use Beman scripts and tools. The CMakePresets.txt exists largely to support those tools. I find the workflow [Makefile](./Makefile) easier to extend with less combanitorial explosion.

Complers are expected to be available on PATH with versioned names, such as `g++-15` or `clang++-21`. Toolchains are in the ./etc/ directory.

`make` by itself uses the system `c++` compiler. For others, e.g., `make TOOLCHAIN=gcc-15` will use the etc/gcc-15-toolchain.cmake toolchain, which sets CXX to be gcc-15. By default the build and test is address sanitized, plus some compatible sanitizers. Alternatives are specified with CONFIG, e.g. `make TOOLCHAIN=gcc-15 CONFIG=RelWithDebInfo`.


# Building Presentations with Emacs and Org-Transclusion

This example project uses [nobiot's org-transclusion](https://github.com/nobiot/org-transclusion) and org-export to produce an HTML file for use in presentations. This allows checking that the code is correct but also limited to what is useful.

The export can be run by `make presentation`, which builds and runs the tests for the project and runs the org export afterwards.

The `infra` directory is vendored in from the Beman Project via `git subtree`.

The makefile provides a variety of tools. It will install most borrowing from PyPI as long as `uv` is available. The installation is in a local `.venv` so as not to mess up the rest of your environment.

```shell
(compile-time-forth) sdowney@pwyll:~/src/surround/compile-time-forth (main ±)
$ make help
clean                          Clean the build artifacts
clean-reconf                   Delete the current configured build tree
clean-venv                     Delete python virtual env
compile                        Compile the project
compile_commands.json          symlink the current compile commands db
compile-headers                Compile the headers
coverage                       Build and run the tests with the GCOV profile and process the results
ctest                          Run CTest on current build
dev-shell                      Shell with the venv activated
docs                           Build the docs with Doxygen
help                           Show this help.
install                        Install the project
install-uv                     install uv via `pipx install uv`
lint                           Run all configured tools in pre-commit
lint-manual                    Run all manual tools in pre-commit
mrdocs                         Build the docs with MrDocs
papers                         Build the vendored WG21 papers
reconf                         Recreate the current configured build tree
realclean                      Delete the generated build infrastructure
show-venv                      Debugging target - show venv details
test                           Rebuild and run tests
testinstall                    Test the installed package
venv                           Create python virtual env
view-coverage                  View the coverage report
```

`docs` and `mrdocs` are not included in the example at the moment.

`lint` uses pre-commit to drive the various lint tools.

Use this project as you see fit.

The code in infra is Apache 2.0 licensed, see https://github.com/bemanproject/infra for more details. The CMakeLists.txt is derived from https://github.com/bemanproject/exemplar the purpose of which is to be a concrete but boring example of a well behaved CMake C++ project using the current tools and practices.

The css in `etc/`  is exported from emacs based on the modus tinted themes via `org-html-htmlize-generate-css` .

The Makefile that drives the workflow is mine, is Apache 2.0 licensed, and take what you need from it. No part of it is interesting enough to be protected.
