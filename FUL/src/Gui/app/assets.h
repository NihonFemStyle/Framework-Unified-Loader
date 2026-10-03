#pragma once
#include <d3d11.h>
#include "gfx/image.h"

namespace app {

struct Assets {
    gfx::Image tf2Icon;
    gfx::Image tf2Hero;
    gfx::Image banner;
    gfx::Image splash;
    gfx::Image logo;
    gfx::Image framework;
    gfx::Image astral;
    gfx::Image phobia;
    gfx::Image jvnkbin;

    void load(ID3D11Device* dev);
    void unload();
};

}