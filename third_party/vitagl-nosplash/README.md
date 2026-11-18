# VitaGL without boot splash

`lib/libvitaGL.a` is VitaGL commit `ed51b21` (the exact version shipped in the
`vitasdk/vitasdk:latest` image used here; its splash shows `#ed51b21`) built with
`make NO_SPLASHSCREEN=1 HAVE_SHADER_CACHE=1`. Exported symbols are identical to the VitaSDK build
except the splash-screen ones. Rebuild:

    git -C third_party/vitagl-nosplash-build checkout ed51b21
    docker run ... -w /src/third_party/vitagl-nosplash-build vitasdk/vitasdk:latest make NO_SPLASHSCREEN=1 HAVE_SHADER_CACHE=1

VitaGL is LGPL-2.1 (see COPYING.LESSER in the build tree); static linking
requires offering the object files / allowing relinking when distributing.

HAVE_SHADER_CACHE stores compiled GXP shaders in ux0:data/shader_cache/<TITLEID>/ so later launches skip the runtime Cg compiler.
