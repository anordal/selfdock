# selfdock
A sandbox for process and filesystem isolation, like Docker, but without its hard problems:
Doesn't give or require root,
cleans up used containers after itself,
without being slow at that,
can run off the host's root filesystem,
and can run within itself.

## Principles
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

It is intended that in a special *build* mode, the root filesystem will be writable, but that's for building the root filesystem.

### No daemon
The daemon is what complicates docker in docker.

## Usage examples

    selfdock run sh

    selfdock -v /mnt/data /mnt/data run sh

    selfdock --rootfs /mnt/debian run sh

## Dependencies
* [narg](https://github.com/anordal/narg)

## Compile & install

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

    ninja uninstall

## To do
* selfdock enter
* selfdock build
