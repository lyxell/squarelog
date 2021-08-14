<p align="center">
  <img width="260px" src="https://raw.githubusercontent.com/lyxell/logifix/master/.github/logifix-logo-beam.svg" alt="Logifix">
</p>

# Logifix

Logifix is a fast static analysis tool for Java that fixes violations
automatically.

<ul> </ul>

## Demo

https://user-images.githubusercontent.com/4975941/126644571-3215f01b-40f6-4278-9752-fe988c5e0367.mp4

<ul> </ul>

## Getting started

1. Watch [the demo](#demo)

2. Install

> You can also install from source, see [building](#building).

```bash
# Ubuntu/Debian
curl -L https://github.com/lyxell/logifix/releases/latest/download/logifix-x86_64-linux-gnu.gz | gunzip -c - > /tmp/logifix
chmod +x /tmp/logifix
sudo mv /tmp/logifix /usr/local/bin
```
```bash
# macOS
curl -L https://github.com/lyxell/logifix/releases/latest/download/logifix-x86_64-macos.gz | gunzip -c - > /tmp/logifix
chmod +x /tmp/logifix
sudo mv /tmp/logifix /usr/local/bin
```

3. Run `logifix path/to/your/java/project`

<ul> </ul>

## Features

### Intelligent equational reasoning

Logifix is more than a search-and-replace system. It achieves intelligent equational reasoning
through the use of articulation points in the rewrite graph.

### Speed

Logifix is implemented in [a high-performance Datalog
dialect](https://github.com/souffle-lang/souffle) that is
synthesized into multi-threaded C++ code. It is heavily parallelized even
when working on a single file and usually analyzes
large projects of thousands of files in a few seconds on modern hardware.
If your project is slow to analyze it is considered a bug and you should
[file a bug report](https://github.com/lyxell/logifix/issues/new).

### Mergeability

Logifix is engineered to produce human-like patches that are ready-to-merge by design without
requiring manual modifications.

<ul> </ul>

## Building

### Ubuntu

* `sudo apt install -y bison cmake re2c mcpp`
* `git clone https://github.com/lyxell/logifix`
* `cd logifix`
* `git submodule update --init --recursive`
* `mkdir build`
* `cmake -S . -B build`
* `cmake --build build`

The logifix binary is now found under build.

### macOS

* `brew install bison cmake re2c mcpp`
* `git clone https://github.com/lyxell/logifix`
* `cd logifix`
* `git submodule update --init --recursive`
* `mkdir build`
* `cmake -S . -B build`
* `cmake --build build`

The logifix binary is now found under build.

<ul> </ul>

## Available rules

See [rules](./rules.md).

