# Ontroller-Hook for SDDT 1.51

Modified mu3io library implementation for **SDDT version 1.51 (Re:Fresh)** using [Segatools](https://gitea.tendokyu.moe/TeamTofuShop/segatools) to add support for the [Ontroller](https://dj-dao.com/jp/ontroller).

The implementation modifies the Segatools implementation at commit `0b81ea38dd` (closest [release](https://gitea.tendokyu.moe/TeamTofuShop/segatools/releases/tag/2026-01-09) at 2026-01-09) so it'll be more likely to work the closer your Segatools version is to this.


## Motivation

Segatools has a more elegant solution for overriding the controller implemetation by using a dynamically linked library to handle calls, which can be seen in the [Mu3IO.NET](https://github.com/SirusDoma/Mu3IO.NET) project. However, I could not get this working on any version past v1.45, as populating the `[mu3io].path` field in the `segatools.ini` causes a crash on startup, even with a minimal dummy .dll file.

This project overrides the built-in mu3io with an implementation heavily based on the [Ontroller logic](https://github.com/SirusDoma/Mu3IO.NET/blob/main/Source/Controller/Ontroller.cs) from Mu3IO, but ported back into native C, similar to an earlier but outdated [ontroller-hook](https://gitea.tendokyu.moe/phantomlan/ontroller-hook) project.

It's possible the problem is only on my system, but in case anyone else has a similar issue, hopefully this can provide a stopgap solution until it is properly fixed.

## Download
The **mu3hook.dll** file can be downloaded on the **[release page](https://github.com/espaspw/ontroller-hook-sddt-1.51/releases/latest)**.

## Caveats

I only copied over parts of the original keyboard input logic, and omitted the xInput logic entirely. Hopefully this shouldn't be an issue if you were planning on using an Ontroller anyway, but feel free to raise an issue if you really want controller support back.

## Building

Building the project requires building Segatools. This project is a subtree of the [original project](https://gitea.tendokyu.moe/TeamTofuShop/segatools), but filtered specifically on the directory `games/mu3io`.

Follow the original [build instructions](https://gitea.tendokyu.moe/TeamTofuShop/segatools/src/branch/develop/doc/development.md) on the project, but replace the files in that directory with this repo, and the output file can be found under `build/build64/games/mu3hook/mu3hook.dll`.

The build instructions might be a bit outdated though. Here's what I found to work in the root of the project:
```
docker build .
# Get hash of created image
docker images
docker run -it --rm -v .:/segatools <image-hash>
```

