# selfdock
Provides process- and filesystem isolation for a process and its children.

Alternative to Docker, similar in spirit to Rocket, but less ambitious.

## Motivation
### No privilege escalation
Your `uid` and privileges is the same within the container (thus the name). Selfdock is a suid executable, but drops its privileges prior to invoking the target program. The idea is that voluntarily jailing oneself should not require privileges.

With Docker, users need to be privileged to run it, because it lets users both map the host filesystem into the container, and be root inside the container. So if you let users run Docker on your server, you might just as well give them the root password.

### Image instances, not containers
Docker containers are stateful, the user needs to manage them, and their creation/deletion takes quite some disk IO.
All this is unnecessary given that users are better off having state elsewhere anyway (such as a volume).

With Selfdock, the on-disk format of an image instance is a readonly overlayfs mounted in tmpfs, sharing immutable state between instances of the same image. Memory friendly and doesn't even touch the harddisk. I can spawn >200 instances per second on my slow laptop. That's a couple of magnitudes faster than Docker. Microscaling, yay!

## Usage example
From `/` (yes, using the root filesystem as an image), create a jail instance named `label` and run `sh`:

    selfdock / label sh

## Compile & install

    make
    sudo make install

## To do
* Support volumes (and consider security implications of combining this with that suid thing).
* Find a way to enter an image instance. Extend syntax for that.
