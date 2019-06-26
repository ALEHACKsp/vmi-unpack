# VMI-Unpack

[![Build Status](https://travis-ci.org/carter-yagemann/vmi-unpack.svg?branch=master)](https://travis-ci.org/carter-yagemann/vmi-unpack)

## About

VMI-Unpack is a virtual machine introspection (VMI) based generic unpacker. Its
design is inspired by projects like [Ether](http://ether.gtisc.gatech.edu/) and
[DRAKVUF](https://github.com/tklengyel/drakvuf).

VMI-Unpack is intended for reverse engineering binaries that are obfuscated with
a technique known as packing. Unlike Ether, VMI-Unpack leverages actively
maintained projects like [libVMI](https://github.com/libvmi/libvmi) and
[rekall](https://github.com/google/rekall). Compared to Ether, which has been
stuck supporting only Xen 4.3 for over a decade, VMI-Unpack should be
significantly easier to maintain and extend.

VMI-Unpack is also MIT licensed so everyone can use and modify it.

## Support

VMI-Unpack current works under the following conditions:

Hypervisors:
* Xen

Host OS:
* Linux

Virtual Machine OS:
* Windows
* Linux

Paging Modes:
* IA32E

## Installation

VMI-Unpack depends on [libVMI](https://github.com/libvmi/libvmi) and
[rekall](https://github.com/google/rekall), so these must be installed and
configured first along with a compatible hypervisor. You will also have to make
a libVMI configuration and rekall JSON profile for the virtual machine you wish
to perform unpacking on. Refer to both projects for more details.

VMI-Unpack additionally depends on the glib-2.0, json-glib-1.0, openssl, and the standard
build utilities. The following is an example of how to install these
dependencies on Debian using apt:

    sudo apt install build-essential libglib2.0-dev libjson-glib-dev libssl-dev

For running unit tests, CUnit is also required:

    sudo apt install libcunit1-dev

Once all the dependencies are installed, simply download or clone this
repository and run `make`.

## Usage

```
./bin/unpack [options]

Required arguments:
    -d <domain_name>         Name of VM to unpack from.
    -r <rekall_file>         Path to rekall file.
    -o <output_dir>          Directory to dump layers into.

One of the following must be provided:
    -p <pid>                 Unpack process with provided PID.
    -n <process_name>        Unpack process with provided name.

Optional arguments:
    -f                       Also follow children created by target process.
    -l                       Monitor library, heap and stack pages. By default, these are ignored.
```

## Output

Every time the target process writes data to a page and then executes it, a full memory
dump will be taken for that process' userspace. When the dump includes more than one
continuous chunk of memory, a `.map` file will also be written with meta-data like the
file offset of each memory chunk, the virtual address each chunk came from, the value in
the program counter, etc.

## Instrumentation

For users that wish to unpack many programs in an automated fashion, this project
provides basic server and client agents under the `agent/` directory. The following
subsections explain how to setup and use this agent. The process is similar to setting
up a [Cuckoo Sandbox](https://cuckoosandbox.org/) environment.

### Before You Begin

Before setting up the agent, you should first make sure you can compile and run
VMI-Unpack manually. This will ensure that problems you encounter are specific to
the agent setup and not libVMI, Xen, etc.

### Preparing the Host

The provided server agent is designed for Xen running on a Linux host and makes use of
the following programs:

* libvirt

* qemu-img

* python (2.x or 3.x)

These should be installed on your host before continuing.

Next, you need to create a configuration so the agent knows which virtual machine
to use and where the relevant files are located. See `agent/example.conf` for a
commented example. The `xml_conf` should be a libvirt configuration. The easiest
way to create one is to define a VM using virsh and then dump the XML.
Note that the agent currently only supports using a single guest VM and its main
storage file must be a QCOW2.

The host is now ready.

### Preparing the Guest

To prepare the guest, simply install python (tested with version 2.x), copy over
the script `agent/agent.py`, and configure the guest to run it at startup. For
example, on Windows you can place `agent.py` in the user's startup directory.

Upon running, the client agent will try to connect to the default gateway to
retrieve samples. Therefore, this is the interface the server agent should be
configured to bind to (`host_ip` in `agent/example.conf`).

Unlike Cuckoo, you do not need a snapshot to use this agent. Once configured,
the guest VM should be powered off.

### Running Samples

See `agent/server.py` for usage:

```
Usage: server.py [OPTIONS]

  Main method

Options:
  -c, --conf FILENAME            Path to configuration (default:
                                 ./example.conf)
  -l, --log-level INTEGER RANGE  Logging level (10: Debug, 20: Info, 30:
                                 Warning, 40: Error, 50: Critical) (default:
                                 Info)
  --dry-run                      Show commands that would run (logged at debug
                                 level) instead of actually running them
  -o, --outdir PATH              Path to store all output data and logs
                                 [required]
  -s, --sample PATH              Path to sample file or directory of files to
                                 unpack  [required]
  --help                         Show this message and exit.
```
