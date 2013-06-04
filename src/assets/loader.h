#ifndef LOADER_H
#define LOADER_H

#include "util.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

namespace pho {

class MeshLoader {
public:
    MeshLoader();
private:
    std::string assetpath;

};

}
#endif
