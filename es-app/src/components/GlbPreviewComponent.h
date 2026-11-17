#pragma once
#include "GuiComponent.h"
#include <vector>
#include <string>
#include <cstdint>
#include <array>

class GlbPreviewComponent : public GuiComponent {
public:
    explicit GlbPreviewComponent(Window* window);
    ~GlbPreviewComponent() override;
    bool setModel(const std::string& path);
    // GLB_XML_ZOOM_V1
    void setZoom(float zoom) { mZoom = zoom; }
    void update(int deltaTime) override;
    void render(const Transform4x4f& parentTrans) override;
private:
    struct Vertex { float pos[3], normal[3], uv[2]; float color[4]={1,1,1,1}; float bindPos[3]={0,0,0}; unsigned short joint[4]={0,0,0,0}; float weight[4]={0,0,0,0}; };
    struct Part { int skin = -1; std::vector<Vertex> verts; int image = -1; int wrapS = 10497, wrapT = 10497; float color[4] = {1,1,1,1}; bool doubleSided=false; bool blend=false; bool alphaMask=false; float alphaCutoff=0.5f; };
    struct Picture { std::vector<unsigned char> encoded; unsigned int texture = 0; };
    // GLB_CPU_SKIN_V1: animacao esquelética por CPU, sem shaders novos.
    using Mat4 = std::array<float,16>;
    struct BoneNode {
        int parent=-1; float t[3]={0,0,0}; float r[4]={0,0,0,1}; float s[3]={1,1,1};
        bool hasMatrix=false; Mat4 matrix={};
    };
    struct AnimTrack {
        int node=-1, path=-1; bool step=false;
        std::vector<float> times, values; // path: 0=translation 1=rotation 2=scale
    };
    std::vector<BoneNode> mBoneNodes;
    std::vector<int> mJointNodes;
    std::vector<Mat4> mInverseBind;
    std::vector<AnimTrack> mAnimTracks;
    float mAnimDuration=0, mAnimTime=0;
    void updateAnimation();
    std::vector<Part> mParts;
    std::vector<Picture> mPictures;
    std::string mPath;
    bool mValid = false;
    float mAngle = 0;
    float mCenter[3] = {0,0,0};
    float mExtent = 1;
    float mBottomY = 0.0f; // GLB_BOTTOM_ANCHOR_V1
    float mZoom = 1.0f;
    void releaseTextures();
    bool load(const std::string& path);
    bool uploadTextures();
};
