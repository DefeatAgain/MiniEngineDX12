#include "glTF.h"
#include "Utils/DebugUtils.h"
#include "Math/VectorMath.h"

#include <fstream>

using namespace glTF;


void ReadFloats( json& list, float flt_array[] )
{
    uint32_t i = 0;
    for (auto& flt : list.GetArray())
        flt_array[i++] = flt.GetFloat();
}

void glTF::Asset::ProcessNodes( json& nodes )
{
    m_nodes.resize(nodes.GetArray().Size());

    uint32_t nodeIdx = 0;

    for (json::ConstMemberIterator it = nodes.MemberBegin(); it != nodes.MemberEnd(); ++it)
    {
        glTF::Node& node = m_nodes[nodeIdx++];
        json& thisNode = it->value;

        node.flags = 0;
        node.mesh = nullptr;
        node.linearIdx = it - nodes.MemberBegin();

        if (thisNode.HasMember("camera"))
        {
            node.camera = &m_cameras[thisNode["camera"].GetUint()];
            node.pointsToCamera = true;
        }
        else if (thisNode.HasMember("mesh"))
        {
            node.mesh = &m_meshes[thisNode["mesh"].GetUint()];
        }

        if (thisNode.HasMember("skin"))
        {
            ASSERT(node.mesh != nullptr);
            node.mesh->skin = thisNode["skin"].GetInt();
        }

        if (thisNode.HasMember("children"))
        {
            const auto& children = thisNode["children"].GetArray();
            node.children.reserve(children.Size());
            for (auto& child : children)
                node.children.push_back(&m_nodes[child.GetUint()]);
        }

        if (thisNode.HasMember("matrix"))
        {
            // TODO:  Should check for negative determinant to reverse triangle winding
            ReadFloats(thisNode["matrix"], node.matrix);
            node.hasMatrix = true;
        }
        else
        {
            if (thisNode.HasMember("scale"))
            {
                ReadFloats(thisNode["scale"], node.scale);
            }
            else
            {
                node.scale[0] = 1.0f;
                node.scale[1] = 1.0f;
                node.scale[2] = 1.0f;
            }

            if (thisNode.HasMember("rotation"))
            {
                ReadFloats(thisNode["rotation"], node.rotation);
            }
            else
            {
                node.rotation[0] = 0.0f;
                node.rotation[1] = 0.0f;
                node.rotation[2] = 0.0f;
                node.rotation[3] = 1.0f;
            }

            if (thisNode.HasMember("translation"))
            {
                ReadFloats(thisNode["translation"], node.translation);
            }
            else
            {
                node.translation[0] = 0.0f;
                node.translation[1] = 0.0f;
                node.translation[2] = 0.0f;
            }
        }
    }
}

void glTF::Asset::ProcessScenes( json& scenes )
{
    m_scenes.resize(scenes.GetArray().Size());

    size_t sceneIdx = 0;
    for (json::ConstMemberIterator it = scenes.MemberBegin(); it != scenes.MemberEnd(); ++it, ++sceneIdx)
    {
        glTF::Scene& scene = m_scenes[sceneIdx];
        json& thisScene = it->value;

        if (thisScene.HasMember("nodes"))
        {
            json& nodes = thisScene["nodes"];
            scene.nodes.resize(nodes.GetArray().Size());
            uint32_t nodeIdx = 0;
            for (json::ConstMemberIterator nodesIt = nodes.MemberBegin(); nodesIt != nodes.MemberEnd(); ++nodesIt, ++nodeIdx)
                scene.nodes[nodeIdx] = &m_nodes[nodesIt->value.GetUint()];
        }
    }
}

void glTF::Asset::ProcessCameras( json& cameras )
{
    m_cameras.resize(cameras.GetArray().Size());

    size_t camIdx = 0;
    for (json::ConstMemberIterator it = cameras.MemberBegin(); it != cameras.MemberEnd(); ++it, ++camIdx)
    {
        glTF::Camera& camera = m_cameras[camIdx];
        json& thisCamera = it->value;

        if (thisCamera["type"] == "perspective")
        {
            json& perspective = thisCamera["perspective"];
            camera.type = Camera::kPerspective;
            camera.aspectRatio = 0.0f;
            if (perspective.HasMember("aspectRatio"))
                camera.aspectRatio = perspective["aspectRatio"].GetFloat();
            camera.yfov = perspective["yfov"].GetFloat();
            camera.znear = perspective["znear"].GetFloat();
            camera.zfar = 0.0f;
            if (perspective.HasMember("zfar"))
                camera.zfar = perspective["zfar"].GetFloat();
        }
        else
        {
            camera.type = Camera::kOrthographic;
            json& orthographic = thisCamera["orthographic"];
            camera.xmag = orthographic["xmag"].GetFloat();
            camera.ymag = orthographic["ymag"].GetFloat();
            camera.znear = orthographic["znear"].GetFloat();
            camera.zfar = orthographic["zfar"].GetFloat();
            ASSERT(camera.zfar > camera.znear);
        }
    }
}

uint16_t TypeToEnum( const char type[] )
{
    if (strncmp(type, "VEC", 3) == 0)
        return Accessor::kVec2 + type[3] - '2';
    else if (strncmp(type, "MAT", 3) == 0)
        return Accessor::kMat2 + type[3] - '2';
    else
        return Accessor::kScalar;
}

void glTF::Asset::ProcessAccessors( json& accessors )
{
    m_accessors.resize(accessors.GetArray().Size());

    size_t accessorIdx = 0;
    for (json::ConstMemberIterator it = accessors.MemberBegin(); it != accessors.MemberEnd(); ++it, ++accessorIdx)
    {
        glTF::Accessor& accessor = m_accessors[accessorIdx];
        json& thisAccessor = it->value;

        glTF::BufferView& bufferView = m_bufferViews[thisAccessor["bufferView"].GetUint()];
        accessor.dataPtr = m_buffers[bufferView.buffer].get()->data() + bufferView.byteOffset;
        accessor.stride = bufferView.byteStride;
        if (thisAccessor.HasMember("byteOffset"))
            accessor.dataPtr += thisAccessor["byteOffset"].GetUint64();
        accessor.count = thisAccessor["count"].GetUint();
        accessor.componentType = thisAccessor["componentType"].GetUint() - 5120;

        char type[8];
        strcpy_s(type, thisAccessor["type"].GetString());

        accessor.type = TypeToEnum(type);
    }
}

void glTF::Asset::FindAttribute( Primitive& prim, json& attributes, Primitive::eAttribType type, const std::string& name )
{
    json::ConstMemberIterator attrib = attributes.FindMember(name.c_str());
    if (attrib != attributes.MemberEnd())
    {
        prim.attribMask |= 1 << type;
        prim.attributes[type] = &m_accessors[attrib->value.GetUint()];
    }
    else
    {
        prim.attributes[type] = nullptr;
    }
}

void glTF::Asset::ProcessMeshes( json& meshes, json& accessors )
{
    m_meshes.resize(meshes.GetArray().Size());

    uint32_t curMesh = 0;
    for (json::ConstMemberIterator meshIt = meshes.MemberBegin(); meshIt != meshes.MemberEnd(); ++meshIt, ++curMesh)
    {
        json& thisMesh = meshIt->value;
        json& primitives = thisMesh["primitives"];

        m_meshes[curMesh].primitives.resize(primitives.GetArray().Size());
        m_meshes[curMesh].skin = -1;
        m_meshes[curMesh].index = meshIt - meshes.MemberBegin();

        uint32_t curSubMesh = 0;
        for (json::ConstMemberIterator primIt = primitives.MemberBegin(); primIt != primitives.MemberEnd(); ++primIt, ++curSubMesh)
        {
            glTF::Primitive& prim = m_meshes[curMesh].primitives[curSubMesh];
            json& thisPrim = primIt->value;

            prim.attribMask = 0;
            json& attributes = thisPrim["attributes"];

            FindAttribute(prim, attributes, Primitive::kPosition, "POSITION");
            FindAttribute(prim, attributes, Primitive::kNormal, "NORMAL");
            FindAttribute(prim, attributes, Primitive::kTangent, "TANGENT");
            FindAttribute(prim, attributes, Primitive::kTexcoord0, "TEXCOORD_0");
            FindAttribute(prim, attributes, Primitive::kTexcoord1, "TEXCOORD_1");
            FindAttribute(prim, attributes, Primitive::kTexcoord2, "TEXCOORD_2");
            FindAttribute(prim, attributes, Primitive::kTexcoord3, "TEXCOORD_3");
            FindAttribute(prim, attributes, Primitive::kColor0, "COLOR_0");
            FindAttribute(prim, attributes, Primitive::kJoints0, "JOINTS_0");
            FindAttribute(prim, attributes, Primitive::kWeights0, "WEIGHTS_0");

            // Read position AABB
            json& positionAccessor = accessors[attributes["POSITION"].GetUint()];
            ReadFloats(positionAccessor["min"], prim.minPos);
            ReadFloats(positionAccessor["max"], prim.maxPos);

            prim.mode = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            prim.indices = nullptr;
            prim.material = nullptr;
            prim.minIndex = 0;
            prim.maxIndex = 0;
            prim.mode = 4;

            if (thisPrim.HasMember("mode"))
                prim.mode = thisPrim["mode"].GetUint();

            if (thisPrim.HasMember("indices"))
            {
                uint32_t accessorIndex = thisPrim["indices"].GetUint();
                json& indicesAccessor = accessors[accessorIndex];
                prim.indices = &m_accessors[accessorIndex];
                if (indicesAccessor.HasMember("max"))
                    prim.maxIndex = indicesAccessor["max"][0].GetUint();
                if (indicesAccessor.HasMember("min"))
                    prim.minIndex = indicesAccessor["min"][0].GetUint();
            }

            if (thisPrim.HasMember("material"))
                prim.material = &m_materials[thisPrim["material"].GetUint()];
        }
    }
}

void glTF::Asset::ProcessSkins( json& skins )
{
    m_skins.resize(skins.GetArray().Size());

    uint32_t skinIdx = 0;
    for (json::ConstMemberIterator it = skins.MemberBegin(); it != skins.MemberEnd(); ++it, ++skinIdx)
    {
        glTF::Skin& skin = m_skins[skinIdx];
        json& thisSkin = it->value;

        skin.inverseBindMatrices = nullptr;
        skin.skeleton = nullptr;

        if (thisSkin.HasMember("inverseBindMatrices"))
            skin.inverseBindMatrices = &m_accessors[thisSkin["inverseBindMatrices"].GetUint()];

        if (thisSkin.HasMember("skeleton"))
        {
            skin.skeleton = &m_nodes[thisSkin["skeleton"].GetUint()];
            skin.skeleton->skeletonRoot = true;
        }

        json& joints = thisSkin["joints"];
        skin.joints.resize(joints.GetArray().Size());
        uint32_t jointIdx = 0;
        for (json::ConstMemberIterator jointIt = joints.MemberBegin(); jointIt != joints.MemberEnd(); ++jointIt, ++jointIdx)
            skin.joints[jointIdx] = &m_nodes[jointIt->value.GetUint()];
    }
}

inline uint32_t floatToHalf( float f )
{
    const float kF32toF16 = (1.0 / (1ull << 56)) * (1.0 / (1ull << 56)); // 2^-112
    union { float f; uint32_t u; } x;
    x.f = Math::Clamp(f, 0.0f, 1.0f) * kF32toF16; 
    return x.u >> 13;
}

uint32_t glTF::Asset::ReadTextureInfo( json& info_json, glTF::Texture* &info )
{
    info = nullptr;

    if (info_json.HasMember("index"))
        info = &m_textures[info_json["index"].GetUint()];

    if (info_json.HasMember("texCoord"))
        return info_json["texCoord"].GetUint();
    else
        return 0;
}

void glTF::Asset::ProcessMaterials( json& materials )
{
    m_materials.resize(materials.GetArray().Size());

    uint32_t materialIdx = 0;

    for (json::ConstMemberIterator it = materials.MemberBegin(); it != materials.MemberEnd(); ++it)
    {
        glTF::Material& material = m_materials[materialIdx];
        json& thisMaterial = it->value;

        material.index = materialIdx++;
        material.flags = 0;
        material.alphaCutoff = floatToHalf(0.5f);
        material.normalTextureScale = 1.0f;

        if (thisMaterial.HasMember("alphaMode"))
        {
            const char* alphaMode = thisMaterial["alphaMode"].GetString();
            if (strcmp(alphaMode, "BLEND") == 0)
                material.alphaBlend = true;
            else if (strcmp(alphaMode, "MASK") == 0)
                material.alphaTest = true;
        }

        if (thisMaterial.HasMember("alphaCutoff"))
            material.alphaCutoff = floatToHalf(thisMaterial["alphaCutoff"].GetFloat());

        if (thisMaterial.HasMember("pbrMetallicRoughness"))
        {
            json& metallicRoughness = thisMaterial["pbrMetallicRoughness"];

            material.baseColorFactor[0] = 1.0f;
            material.baseColorFactor[1] = 1.0f;
            material.baseColorFactor[2] = 1.0f;
            material.baseColorFactor[3] = 1.0f;
            material.metallicFactor = 1.0f;
            material.roughnessFactor = 1.0f;
            for (uint32_t i = 0; i < Material::kNumTextures; ++i)
                material.textures[i] = nullptr;

            if (metallicRoughness.HasMember("baseColorFactor"))
                ReadFloats(metallicRoughness["baseColorFactor"], material.baseColorFactor);

            if (metallicRoughness.HasMember("metallicFactor"))
                material.metallicFactor = metallicRoughness["metallicFactor"].GetFloat();

            if (metallicRoughness.HasMember("roughnessFactor"))
                material.roughnessFactor = metallicRoughness["roughnessFactor"].GetFloat();

            if (metallicRoughness.HasMember("baseColorTexture"))
                material.baseColorUV = ReadTextureInfo(metallicRoughness["baseColorTexture"],
                    material.textures[Material::kBaseColor]);

            if (metallicRoughness.HasMember("metallicRoughnessTexture"))
                material.metallicRoughnessUV = ReadTextureInfo(metallicRoughness["metallicRoughnessTexture"],
                    material.textures[Material::kMetallicRoughness]);
        }

        if (thisMaterial.HasMember("doubleSided"))
            material.twoSided = thisMaterial["doubleSided"].GetBool();

        if (thisMaterial.HasMember("normalTextureScale"))
            material.normalTextureScale = thisMaterial["normalTextureScale"].GetFloat();

        if (thisMaterial.HasMember("emissiveFactor"))
            ReadFloats(thisMaterial["emissiveFactor"], material.emissiveFactor);

        if (thisMaterial.HasMember("occlusionTexture"))
            material.occlusionUV = ReadTextureInfo(thisMaterial["occlusionTexture"],
                material.textures[Material::kOcclusion]);

        if (thisMaterial.HasMember("emissiveTexture"))
            material.emissiveUV = ReadTextureInfo(thisMaterial["emissiveTexture"],
                material.textures[Material::kEmissive]);

        if (thisMaterial.HasMember("normalTexture"))
            material.normalUV = ReadTextureInfo(thisMaterial["normalTexture"],
                material.textures[Material::kNormal]);
    }
}

void glTF::Asset::ProcessBuffers( json& buffers, ByteArray chunk1bin )
{
    m_buffers.reserve(buffers.GetArray().Size());

    for (json::ConstMemberIterator it = buffers.MemberBegin(); it != buffers.MemberEnd(); ++it)
    {
        json& thisBuffer = it->value;

        if (thisBuffer.HasMember("uri"))
        {
            std::filesystem::path uri = thisBuffer["uri"].GetString();
            std::filesystem::path filepath = m_basePath / uri;

            //ASSERT(ba->size() > 0, "Missing bin file %ws", filepath.c_str());
            m_buffers.emplace_back(Utility::ReadFileAsync(filepath));
        }
        else
        {
            ASSERT(it == buffers.MemberBegin(), "Only the 1st buffer allowed to be internal");
            ASSERT(chunk1bin->size() > 0, "GLB chunk1 missing data or not a GLB file");
            m_trunkBuffer = chunk1bin;
        }
    }
}

void glTF::Asset::ProcessBufferViews( json& bufferViews )
{
    m_bufferViews.reserve(bufferViews.GetArray().Size());

    uint32_t bufViewIdx = 0;
    for (json::ConstMemberIterator it = bufferViews.MemberBegin(); it != bufferViews.MemberEnd(); ++it, ++bufViewIdx)
    {
        glTF::BufferView& bufferView = m_bufferViews[bufViewIdx];
        json& thisBufferView = it->value;

        bufferView.buffer = thisBufferView["buffer"].GetUint();
        bufferView.byteLength = thisBufferView["byteLength"].GetUint();
        bufferView.byteOffset = 0;
        bufferView.byteStride = 0;
        bufferView.elementArrayBuffer = false;

        if (thisBufferView.HasMember("byteOffset"))
            bufferView.byteOffset = thisBufferView["byteOffset"].GetUint();

        if (thisBufferView.HasMember("byteStride"))
            bufferView.byteStride = thisBufferView["byteStride"].GetUint();

        // 34962 = ARRAY_BUFFER;  34963 = ELEMENT_ARRAY_BUFFER
        if (thisBufferView.HasMember("target") && thisBufferView["target"].GetUint() == 34963)
            bufferView.elementArrayBuffer = true;
    }
}

void glTF::Asset::ProcessImages( json& images )
{
    m_images.resize(images.GetArray().Size());

    uint32_t imageIdx = 0;
    for (json::ConstMemberIterator it = images.MemberBegin(); it != images.MemberEnd(); ++it)
    {
        json& thisImage = it->value;
        if (thisImage.HasMember("uri"))
        {
            m_images[imageIdx++].path = thisImage["uri"].GetString();
        }
        else if (thisImage.HasMember("bufferView"))
        {
            Utility::PrintMessage("GLB image at buffer view %d with mime type %s\n", thisImage["bufferView"].GetUint(), 
                thisImage["mimeType"].GetString());
        }
        else
        {
            ASSERT(0);
        }
    }
}

D3D12_TEXTURE_ADDRESS_MODE GLtoD3DTextureAddressMode( uint32_t glWrapMode )
{
    switch (glWrapMode)
    {
    default: ASSERT(false, "Unexpected sampler wrap mode");
    case 33071: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case 33648: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case 10497: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
}

D3D12_FILTER GLtoD3DTextureFilterMode( int32_t magFilter, int32_t minFilter )
{
    bool minLinear = false;
    switch (minFilter)
    {
        case 9728: //nearest
        case 9986: //nearest_mipmap_linear
        case 9984: break;//nearest_mipmap_nearest
        case 9729: //linear
        case 9987: //linear_mipmap_linear
        case 9985: minLinear = true; break;//linear_mipmap_nearest
        default: break;
    }

    bool magLinear = false;
    switch (magFilter)
    {
        case 9728: //nearest
        case 9986: //nearest_mipmap_linear
        case 9984: break;//nearest_mipmap_nearest
        case 9729: //linear
        case 9987: //linear_mipmap_linear
        case 9985: magLinear = true; break;//linear_mipmap_nearest
        default: break;
    }

    if (minLinear && magLinear || magLinear)
        return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    else if (minLinear)
        return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    else
        return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

void glTF::Asset::ProcessSamplers( json& samplers )
{
    m_samplers.resize(samplers.GetArray().Size());

    uint32_t samplerIdx = 0;
    for (json::ConstMemberIterator it = samplers.MemberBegin(); it != samplers.MemberEnd(); ++it)
    {
        json& thisSampler = it->value;

        glTF::Sampler& sampler = m_samplers[samplerIdx++];
        sampler.filter = D3D12_FILTER_ANISOTROPIC;
        sampler.wrapS = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.wrapT = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

        // Who cares what is provided?  It's about what you can afford, generally
        // speaking about materials.  If you want anisotropic filtering, why let
        // the asset dictate that.  And AF isn't represented in WebGL, so blech.
        uint32_t magFilter = 9729;
        uint32_t minFilter = 9729;
        if (thisSampler.HasMember("magFilter"))
            magFilter = thisSampler["magFilter"].GetUint();
        if (thisSampler.HasMember("minFilter"))
            minFilter = thisSampler["minFilter"].GetUint();
        sampler.filter = GLtoD3DTextureFilterMode(magFilter, minFilter);

        // But these could matter for correctness.  Though, where is border mode?
        if (thisSampler.HasMember("wrapS"))
            sampler.wrapS = GLtoD3DTextureAddressMode(thisSampler["wrapS"].GetUint());
        if (thisSampler.HasMember("wrapT"))
            sampler.wrapT = GLtoD3DTextureAddressMode(thisSampler["wrapT"].GetUint());
    }
}

void glTF::Asset::ProcessTextures( json& textures )
{
    m_textures.resize(textures.GetArray().Size());

    uint32_t texIdx = 0;

    for (json::ConstMemberIterator it = textures.MemberBegin(); it != textures.MemberEnd(); ++it)
    {
        glTF::Texture& texture = m_textures[texIdx++];
        json& thisTexture = it->value;

        texture.source = nullptr;
        texture.sampler = nullptr;

        if (thisTexture.HasMember("source"))
            texture.source = &m_images[thisTexture["source"].GetUint()];

        if (thisTexture.HasMember("sampler"))
            texture.sampler = &m_samplers[thisTexture["sampler"].GetUint()];
    }
}

void glTF::Asset::ProcessAnimations(json& animations)
{
    m_animations.resize(animations.GetArray().Size());

    uint32_t animIdx = 0;
    // Process all animations
    for (json::ConstMemberIterator it = animations.MemberBegin(); it != animations.MemberEnd(); ++it)
    {
        json& thisAnimation = it->value;
        glTF::Animation& animation = m_animations[animIdx++];

        // Process this animation's samplers
        json& samplers = thisAnimation["samplers"];
        animation.m_samplers.resize(samplers.GetArray().Size());
        uint32_t samplerIdx = 0;

        for (json::ConstMemberIterator it2 = samplers.MemberBegin(); it2 != samplers.MemberEnd(); ++it2)
        {
            json& thisSampler = it2->value;
            glTF::AnimSampler& sampler = animation.m_samplers[samplerIdx++];
            sampler.m_input = &m_accessors[thisSampler["input"].GetUint()];
            sampler.m_output = &m_accessors[thisSampler["output"].GetUint()];
            sampler.m_interpolation = AnimSampler::kLinear;
            if (thisSampler.HasMember("interpolation"))
            {
                const char* interpolation = thisSampler["interpolation"].GetString();
                if (strcmp(interpolation, "LINEAR") == 0)
                    sampler.m_interpolation = AnimSampler::kLinear;
                else if (strcmp(interpolation, "STEP") == 0)
                    sampler.m_interpolation = AnimSampler::kStep;
                else if (strcmp(interpolation, "CATMULLROMSPLINE") == 0)
                    sampler.m_interpolation = AnimSampler::kCatmullRomSpline;
                else if (strcmp(interpolation, "CUBICSPLINE") == 0)
                    sampler.m_interpolation = AnimSampler::kCubicSpline;
            }
        }

        // Process this animation's channels
        json& channels = thisAnimation["channels"];
        animation.m_channels.resize(channels.GetArray().Size());
        uint32_t channelIdx = 0;

        for (json::ConstMemberIterator it2 = channels.MemberBegin(); it2 != channels.MemberEnd(); ++it2)
        {
            json& thisChannel = it2->value;
            glTF::AnimChannel& channel = animation.m_channels[channelIdx++];
            channel.m_sampler = &animation.m_samplers[thisChannel["sampler"].GetUint()];
            json& thisTarget = thisChannel["target"];
            channel.m_target = &m_nodes[thisTarget["node"].GetUint()];
            const char* path = thisTarget["path"].GetString();
            if (strcmp(path, "translation") == 0)
                channel.m_path = AnimChannel::kTranslation;
            else if (strcmp(path, "rotation") == 0)
                channel.m_path = AnimChannel::kRotation;
            else if (strcmp(path, "scale") == 0)
                channel.m_path = AnimChannel::kScale;
            else if (strcmp(path, "weights") == 0)
                channel.m_path = AnimChannel::kWeights;
        }
    }
}

void glTF::Asset::Parse(const std::filesystem::path& filepath)
{
    // TODO:  add GLB support by extracting JSON section and BIN sections
    //https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#glb-file-format-specification

    ByteArray gltfFile;
    ByteArray chunk1Bin;

    std::filesystem::path fileExt = filepath.extension();

    if (fileExt == L".glb")
    {
        std::ifstream glbFile(filepath, std::ios::in | std::ios::binary);
        struct GLBHeader
        {
            char magic[4];
            uint32_t version;
            uint32_t length;
        } header;
        glbFile.read((char*)&header, sizeof(GLBHeader));
        if (strncmp(header.magic, "glTF", 4) != 0)
        {
            Utility::Print("Error:  Invalid glTF binary format\n");
            return;
        }
        if (header.version != 2)
        {
            Utility::Print("Error:  Only glTF 2.0 is supported\n");
            return;
        }

        uint32_t chunk0Length;
        char chunk0Type[4];
        glbFile.read((char*)&chunk0Length, 4);
        glbFile.read((char*)&chunk0Type, 4);
        if (strncmp(chunk0Type, "JSON", 4) != 0)
        {
            Utility::Print("Error: Expected chunk0 to contain JSON\n");
            return;
        }
        gltfFile = std::make_shared<std::vector<byte>>( chunk0Length + 1 );
        glbFile.read((char*)gltfFile->data(), chunk0Length);
        (*gltfFile)[chunk0Length] = '\0';

        uint32_t chunk1Length;
        char chunk1Type[4];
        glbFile.read((char*)&chunk1Length, 4);
        glbFile.read((char*)&chunk1Type, 4);
        if (strncmp(chunk1Type, "BIN", 3) != 0)
        {
            Utility::Print("Error: Expected chunk1 to contain BIN\n");
            return;
       }

        chunk1Bin = std::make_shared<std::vector<byte>>(chunk1Length);
        glbFile.read((char*)chunk1Bin->data(), chunk1Length);
    }
    else 
    {
        ASSERT(fileExt == L".gltf");

        // Null terminate the string (just in case)
        gltfFile = Utility::ReadFileSync(filepath);
        if (gltfFile->size() == 0)
            return;

        gltfFile->push_back('\0');
        chunk1Bin = std::make_shared<std::vector<byte>>(0);
    }

    rapidjson::Document document;
    document.Parse((const char*)gltfFile->data());
    if (!document.IsObject())
    {
        Utility::PrintMessage("Invalid glTF file: %s\n", filepath.c_str());
        return;
    }

    // Strip off file name to get root path to other related files
    m_basePath = filepath.parent_path();

    // Parse all state
    json& root = document.GetObject();
    if (root.HasMember("buffers"))
        ProcessBuffers(root["buffers"], chunk1Bin);
    if (root.HasMember("bufferViews"))
        ProcessBufferViews(root["bufferViews"]);
    if (root.HasMember("accessors"))
        ProcessAccessors(root["accessors"]);
    if (root.HasMember("images"))
        ProcessImages(root["images"]);
    if (root.HasMember("samplers"))
        ProcessSamplers(root["samplers"]);
    if (root.HasMember("textures"))
        ProcessTextures(root["textures"]);
    if (root.HasMember("materials"))
        ProcessMaterials(root["materials"]);
    if (root.HasMember("meshes"))
        ProcessMeshes(root["meshes"], root["accessors"]);
    if (root.HasMember("cameras"))
        ProcessCameras(root["cameras"]);
    if (root.HasMember("nodes"))
        ProcessNodes(root["nodes"]);
    if (root.HasMember("skins"))
        ProcessSkins(root["skins"]);
    if (root.HasMember("scenes"))
        ProcessScenes(root["scenes"]);
    if (root.HasMember("animations"))
        ProcessAnimations(root["animations"]);
    if (root.HasMember("scene"))
        m_scene = &m_scenes[root["scene"].GetUint()];
}
