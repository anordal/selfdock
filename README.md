# selfdock
A sandbox for process and filesystem isolation, like Docker, but without its hard problems:
Doesn't give you root, isn't slow at spawning and tearing down containers, can use the host's root filesystem as an image, and can run within itself.

## Principles
### Stateless instances
Give up the idea of *data volume containers*. Given that *volumes* are the way to go,
no other filesystems in the container need to, or should be, writable. So forbid state in containers and exploit the advantages:
* No risk of deleting anything useful: Stateless containers can be cleaned up without asking. Because the user won't be thinking in containers, they will be called *instances* instead.
* No disk writes: There is no write backing or anything to copy/delete on spawn/teardown. I can spawn >200 instances per second on my slow laptop, which is over 100 times faster than Docker. Microscaling, yay!
* Memory friendly, sharing immutable state between instances of the same image.

It is intended that in a special *build* mode, the root filesystem will be writable, but that's for building the root filesystem.

### No privilege escalation
With support for volumes, it is important that your `uid` and privileges are the same within the container. Selfock enforces this, thus the name. With Docker, you become root inside the container, which is why only root can safely be allowed to use it - adding users to the `docker` group is really like giving them the root password! Selfdock is a suid executable, but drops its privileges prior to invoking the target program. The idea is that voluntarily jailing oneself should not require privileges.

### No daemon
The daemon is what complicates docker in docker. To be avoided if possible.

## Usage examples

    selfdock run sh

    selfdock -v /mnt/data /mnt/data run sh

    selfdock --root /mnt/debian run sh

## Dependencies
* [narg](https://github.com/anordal/narg)

## Compile & install

    make
    sudo make install

### Run tests

Note: Tests *the installed* program (because Selfdock is a suid program, it won't work before it is installed):

    make -j test

## To do
* selfdock enter
* selfdock build
