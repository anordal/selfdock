# selfdock
A sandbox for process and filesystem isolation, like Docker, but without its hard problems:
Doesn't give or require root,
cleans up used containers after itself,
without being slow at that,
can run off the host's root filesystem,
and can run within itself.

## Suitability
Docker is too slow for use in commads supposed to be run interactively (such as compiler toolchains) due to disk abuse. Here's with a poor 5400 rpm harddisk for great effect:

    > time docker run --rm -it ubuntu true
    0.04user 0.02system 0:04.19elapsed 1%CPU (0avgtext+0avgdata 27428maxresident)k
    0inputs+0outputs (0major+1664minor)pagefaults 0swaps

Selfdock does not touch the platters, and finishes instantly:

    > time selfdock --rootfs /opt/os/ubuntu run true
    0.00user 0.00system 0:00.01elapsed 0%CPU (0avgtext+0avgdata 1136maxresident)k
    0inputs+0outputs (0major+112minor)pagefaults 0swaps

And doesn't need a separate root filesystem (omitting --rootfs is like setting --rootfs=/):

    > selfdock run true

Selfdock is optimized for running one-off commands, but can of course also run daemons.
This is an intended side-effect – equally valuable, just no focus of optimization.

## Usage examples

    selfdock --help

Run `sh` in a container (yes, interactive commands just work, and you don't need an "image" first).
Observe that this shell becomes pid 1, and that only the root filesystem is visible.

    selfdock run sh
    sh$ htop
    sh$ df -h

To make other filesystems appear (possibly writable) somewhere inside the container:

    selfdock --map /mnt/rodata /mnt/rodata \
             --vol /mnt/rwdata /mnt/rwdata run sh

Use a different root filesystem, say from another Linux installation:

    selfdock --rootfs /mnt/debian run sh

[Extract a docker image](https://github.com/larsks/undocker) and use it as a root filesystem:

    docker save busybox | undocker -i -o /tmp/busybox busybox
    sudo mkdir /opt/os/
    sudo mv /tmp/busybox /opt/os/
    selfdock --rootfs /opt/os/busybox run sh

Sandbox demo: Observe that `eject` only works if you give it access to /dev:

    eject # Opens your CD tray
    eject -t # Closes it

    selfdock --rootfs /opt/os/busybox run eject # Shall fail (missing /dev/cdrom)

    selfdock --rootfs /opt/os/busybox -v /dev /dev run eject

In a special *build* mode, the root filesystem is writable (for building the root filesystem):

    selfdock --rootfs /opt/os/debootstrap build …

## Design Principles
### No privilege escalation
Dilemma: To do what Selfdock (and Docker) does requires root privileges,
even if your goal may be the opposite,
strictly *reducing* access to the rest of the system.
It should not require root privileges to jail oneself!

Unlike Docker, Selfdock aims to solve this dilemma. Compare:

* Docker gives you root like a passwordless sudo
and lets you tamper with the host system's files via volumes,
which is why only root can safely be allowed to use Docker.

* Selfdock runs the target program as the invoking user ID (thereby the name).
This is important when accessing the same files across container boundaries,
such as in volumes, or running off the host filesystem for that matter,
because access rights are determined by the user and group IDs of those files,
and they stay the same across container boundaries!

The other solution, user ID translation of processes aka.
[user namespaces](https://blog.yadutaf.fr/2016/04/14/docker-for-your-users-introducing-user-namespace/),
is supported by Docker, but not obligatory.

Selfdock is a suid executable, out of necessity,
but because of this great responsibility,
ensuring that the user doesn't get any increased access
(e.g. to files or processes) is its top priority.

### Stateless instances
Give up the idea of *data volume containers*. Given that *volumes* are the way to go,
no other filesystems in the container need to, or should be, writable. So forbid state in containers and exploit the advantages:
* No risk of deleting anything useful: Stateless containers can be cleaned up without asking. Because the user won't be thinking in containers, they will be called *instances* instead.
* No disk writes: There is no write backing or anything to copy/delete on spawn/teardown. I can spawn >200 instances per second on my slow laptop, which is over 100 times faster than Docker. Microscaling, yay!
* Memory friendly – sharing immutable state between instances of the same image.

### No daemon
The daemon is what complicates docker in docker.

## Build
### Dependencies
* [narg](https://github.com/anordal/narg)

### Compile & install

    mkdir /tmp/build-selfdock
    meson /tmp/build-selfdock --buildtype=release
    cd    /tmp/build-selfdock
    ninja
    sudo ninja install

Replace meson with meson.py if you installed it via `pip3 install meson`.

### Run tests

Note: Tests *the installed* program:

    ninja moduletest

### Uninstall

    ninja uninstall-actual

(I can't call it *uninstall* because it's [taken](https://github.com/mesonbuild/meson/issues/753) and can't be hooked into yet.)

## To do
* `selfdock enter`: A way to enter a running instance, like `docker exec -it`. Should be possible already with `nsenter`, dunno how yet.
