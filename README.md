# Return of the Shadow

This is the current live code for the MUD Return of the Shadow. The majority of
the code base is C++, but there is still some C that is used to generate the
random maze files and the PK Fame.

## Getting Started

These instructions will get you a copy of the project up and running on your
local machine for development and testing purposes. See deployment for notes on
how to deploy the project on the live system.

### Prerequisites

On your Unix based system you'll need to install the following packages.

1. gcc (This is needed to create the C files)
2. g++ (This is needed for the main game compiler)
3. clang-format (We use  this to format all the code base)
4. make (This is just something you should have in general)
5. cmake (Used by the root Makefile and the direct CMake workflow)

### Installing

Below is a step by step series that help you setup your development environment

#### Step 1: Fork the Project

First you'll need to fork this repository, and create a local clone of that
fork. [please follow these instructions on how to fork a
project](https://help.github.com/articles/fork-a-repo/)

#### Step 2: Setup the Player Files

After you have successfully forked this repository, you'll need to setup the
games files. In your terminal navigate to the local repository and run the
following commands.

```bash
make setup
```

Or directly with CMake from the repository root:

```bash
cmake -S src -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build --target setup
```

This will create all user folder structure that the game needs to run. This
will not important any characters to the game, so the first character created
will be promoted to a level 100 Implementor.

#### Step 3: Setting up the World Files

We keep the world files in a separate git repository to keep from having merge
conflicts with the main game code.

You'll need to fork the following repository
[https://github.com/Noobinabox/RotS-WorldFiles](https://github.com/Noobinabox/RotS-WorldFiles)

Once you have successfully forked the project, copy the files into the main
code root directory.

#### Step 4: Compiling the Game

Once all the game files are setup from Step 2 you'll need to compile the game.
In your terminal navigate to the local repository and run the following
commands.

```bash
make build
```

Or directly with CMake from the repository root:

```bash
cmake -S src -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build --target ageland
```

> You'll see tons of notifications of deprecated functions, but the game should
> compile none the less.

This will compile all the code and create an executable called ageland in the
./bin folder.

#### Step 4a: Running the Unit Tests

For the C++ unit tests you can use either workflow.

```bash
make test
```

Or directly with CMake from the repository root:

```bash
cmake -S src -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build --target ageland_tests
ctest --test-dir build --output-on-failure
```

#### Step 4b: Running the Account Smoke Test

The account/login smoke flow is kept separate from `make test` so unit tests stay
fast and stable. Run it manually when validating account, login, authentication,
or character-selection changes.

```bash
make smoke-account
```

## GitHub Actions

This repository includes a GitHub Actions workflow that runs on pushes to
`master` and pull requests targeting `master`. It builds the game, runs the C++
unit tests, and then runs the proxy-backed account smoke flow.

If you want GitHub to block merges until those checks pass, enable branch
protection for `master` in the repository settings and mark the `Build, Unit
Tests, and Smoke Tests` job from the CI workflow as a required status check.
If you also want to block direct pushes to `master`, make sure your branch
protection or ruleset disables direct-push bypass as well.

#### Step 5: Running the Game

From the repository root you can run the following command.

```bash
make run
```

Or if you want you can still run the binary directly from the root directory

```bash
./bin/ageland -p 3791 &
```

Either command will start the game up and put it in a background process.

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.MD) for details on our code of
conduct, and the process for submitting pull request to us.

## Releases and Design Documentation

All releases should be documented on what was changed and added. Please don't
documentation line for line what you changed but a summarization so that we can
present it to the end-users. You can find all the release notes here.

* [RotS Code Release Builds](release-notes/README.md)

Design documentation should be added to the following location.

* [RotS Design Documentation](game%20design%20docs/README.md)

## Authors

* **Seth Lyon** [Noobinabox](https://github.com/Noobinabox)
* **David Gurley** [drelidan7](https://github.com/drelidan7)
* **Andrew Humbert** [ahumbert](https://github.com/ahumbert)
