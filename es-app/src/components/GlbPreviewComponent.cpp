// GLB_CPU_SKIN_V1
// GLB_SAMPLER_V1
#include "components/GlbPreviewComponent.h"
#include "renderers/Renderer.h"
#include "Log.h"
#include <rapidjson/document.h>
#include <FreeImage.h>
#include <GLES/gl.h>
#include <fstream>
#include <cstdio>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

namespace {
using J = rapidjson::Value;
uint32_t u32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
int num(const J& v, const char* k, int def = 0) { return v.IsObject() && v.HasMember(k) && v[k].IsNumber() ? v[k].GetInt() : def; }
std::array<float,16> identity() { return {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}}; }
std::array<float,16> multiply(const std::array<float,16>& a, const std::array<float,16>& b) {
    std::array<float,16> r = {{0}};
    for (int c=0;c<4;++c) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k) r[c*4+row] += a[k*4+row]*b[c*4+k];
    return r;
}
std::array<float,16> nodeMatrix(const J& n) {
    auto m = identity();
    if(n.HasMember("matrix") && n["matrix"].IsArray() && n["matrix"].Size()==16) {
        for(int i=0;i<16;++i) m[i]=n["matrix"][i].GetFloat();
        return m;
    }
    float x=0,y=0,z=0,w=1;
    if(n.HasMember("rotation") && n["rotation"].IsArray() && n["rotation"].Size()==4) {
        x=n["rotation"][0].GetFloat(); y=n["rotation"][1].GetFloat(); z=n["rotation"][2].GetFloat(); w=n["rotation"][3].GetFloat();
    }
    m[0]=1-2*(y*y+z*z); m[1]=2*(x*y+z*w); m[2]=2*(x*z-y*w);
    m[4]=2*(x*y-z*w); m[5]=1-2*(x*x+z*z); m[6]=2*(y*z+x*w);
    m[8]=2*(x*z+y*w); m[9]=2*(y*z-x*w); m[10]=1-2*(x*x+y*y);
    if(n.HasMember("scale") && n["scale"].IsArray() && n["scale"].Size()==3) {
        for(int c=0;c<3;++c) for(int row=0;row<3;++row) m[c*4+row]*=n["scale"][c].GetFloat();
    }
    if(n.HasMember("translation") && n["translation"].IsArray() && n["translation"].Size()==3)
        for(int i=0;i<3;++i) m[12+i]=n["translation"][i].GetFloat();
    return m;
}
}

GlbPreviewComponent::GlbPreviewComponent(Window* window) : GuiComponent(window) { setVisible(false); }
GlbPreviewComponent::~GlbPreviewComponent() { releaseTextures(); }
void GlbPreviewComponent::releaseTextures() {
    // Never call GL when no context exists: textures are created lazily in render().
    for(auto& p:mPictures) if(p.texture) { GLuint t=p.texture; glDeleteTextures(1,&t); p.texture=0; }
}
bool GlbPreviewComponent::setModel(const std::string& path) {
    if(path==mPath) return mValid;
    releaseTextures(); mPictures.clear(); mParts.clear();
    mBoneNodes.clear(); mJointNodes.clear(); mInverseBind.clear(); mAnimTracks.clear(); mAnimDuration=0; mAnimTime=0;
    mPath=path; mValid=false; setVisible(false);
    if(path.empty() || path=="unknown") return false;
    std::fprintf(stderr,"[GLBDBG] solicitando: %s\n",path.c_str());
    mValid=load(path); setVisible(mValid);
    std::fprintf(stderr,"[GLBDBG] load=%d partes=%zu texturas=%zu extensao=%f\n",int(mValid),mParts.size(),mPictures.size(),mExtent);
    std::fflush(stderr);
    if(!mValid) LOG(LogWarning) << "GLB preview: could not load " << path;
    return mValid;
}
void GlbPreviewComponent::update(int ms) {
    GuiComponent::update(ms);
    if(mValid) {
        mAngle+=ms*0.018f; if(mAngle>360) mAngle=std::fmod(mAngle,360.f);
        if(!mAnimTracks.empty()) { mAnimTime += ms*0.001f; if(mAnimDuration>0) mAnimTime=std::fmod(mAnimTime,mAnimDuration); updateAnimation(); }
    }
}
bool GlbPreviewComponent::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if(!f) return false;
    f.seekg(0,std::ios::end); auto sz=f.tellg();
    if(sz<20 || sz>20*1024*1024) return false; // deliberately bounded for this handheld
    f.seekg(0);
    std::vector<unsigned char> file(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(file.data()),file.size());
    if(!f || std::memcmp(file.data(),"glTF",4) || u32(file.data()+4)!=2 || u32(file.data()+8)!=file.size()) return false;
    size_t off=12; std::string json; std::vector<unsigned char> bin;
    while(off+8<=file.size()) {
        uint32_t len=u32(file.data()+off), type=u32(file.data()+off+4); off+=8;
        if(len>file.size()-off) return false;
        if(type==0x4E4F534A) json.assign(reinterpret_cast<char*>(file.data()+off),len);
        if(type==0x004E4942) bin.assign(file.begin()+off,file.begin()+off+len);
        off+=len;
    }
    if(json.empty() || bin.empty()) return false;
    rapidjson::Document d; d.Parse(json.c_str());
    if(d.HasParseError() || !d.IsObject()) return false;
    for(const char* k:{"bufferViews","accessors","meshes","nodes"}) if(!d.HasMember(k) || !d[k].IsArray()) return false;
    const J& views=d["bufferViews"], &access=d["accessors"], &meshes=d["meshes"], &nodes=d["nodes"];
    auto span = [&](int accessorIndex, int ncomp, int requiredType, size_t& count, size_t& stride, const unsigned char*& base)->bool {
        if(accessorIndex<0 || (size_t)accessorIndex>=access.Size()) return false;
        const J& a=access[accessorIndex];
        if(!a.IsObject() || num(a,"componentType")!=requiredType || num(a,"count")<=0) return false;
        const char* kind=ncomp==16?"MAT4":ncomp==4?"VEC4":ncomp==3?"VEC3":ncomp==2?"VEC2":"SCALAR";
        if(!a.HasMember("type") || !a["type"].IsString() || std::strcmp(a["type"].GetString(),kind)) return false;
        int vi=num(a,"bufferView",-1);
        if(vi<0 || (size_t)vi>=views.Size()) return false;
        const J& v=views[vi];
        if(num(v,"buffer")!=0) return false;
        size_t sizePerComp=requiredType==5126 || requiredType==5125?4:requiredType==5123?2:1;
        size_t packed=ncomp*sizePerComp;
        stride=(size_t)num(v,"byteStride",(int)packed);
        if(stride<packed) return false;
        count=(size_t)num(a,"count");
        size_t vo=(size_t)num(v,"byteOffset"), ao=(size_t)num(a,"byteOffset"), length=(size_t)num(v,"byteLength");
        if(vo>bin.size() || length>bin.size()-vo || ao>length || packed>length-ao) return false;
        if(count>1+(length-ao-packed)/stride) return false;
        base=bin.data()+vo+ao;
        return true;
    };
    auto getFloat=[&](int a, int n, std::vector<float>& out)->bool {
        size_t count=0, stride=0; const unsigned char* p=nullptr;
        if(!span(a,n,5126,count,stride,p)) return false;
        out.resize(count*n);
        for(size_t i=0;i<count;++i) for(int k=0;k<n;++k) std::memcpy(&out[i*n+k],p+i*stride+k*4,4);
        return true;
    };
    auto getIndices=[&](int a, std::vector<uint32_t>& out)->bool {
        if(a<0 || (size_t)a>=access.Size()) return false;
        int ct=num(access[a],"componentType");
        if(ct!=5121 && ct!=5123 && ct!=5125) return false;
        size_t count=0,stride=0; const unsigned char* p=nullptr;
        if(!span(a,1,ct,count,stride,p)) return false;
        out.resize(count);
        for(size_t i=0;i<count;++i) {
            const unsigned char* v=p+i*stride;
            out[i]=ct==5121?v[0]:ct==5123?(uint32_t)(v[0]|v[1]<<8):u32(v);
        }
        return true;
    };
    // GLB_CPU_SKIN_V1: le hierarquia, skin e primeira animacao, mantendo modelos estaticos intactos.
    mBoneNodes.resize(nodes.Size());
    for(rapidjson::SizeType ni=0;ni<nodes.Size();++ni) {
        const J& n=nodes[ni]; auto& b=mBoneNodes[ni];
        auto vec=[&](const char* key,float* out,int count) {
            if(n.HasMember(key) && n[key].IsArray() && n[key].Size()==(rapidjson::SizeType)count)
                for(int k=0;k<count;++k)out[k]=n[key][k].GetFloat();
        };
        vec("translation",b.t,3);vec("rotation",b.r,4);vec("scale",b.s,3);
        if(n.HasMember("matrix") && n["matrix"].IsArray() && n["matrix"].Size()==16) {
            b.hasMatrix=true; for(int k=0;k<16;++k)b.matrix[k]=n["matrix"][k].GetFloat();
        }
        if(n.HasMember("children") && n["children"].IsArray())
            for(auto& ch:n["children"].GetArray())
                if(ch.IsInt() && ch.GetInt()>=0 && (size_t)ch.GetInt()<mBoneNodes.size())mBoneNodes[ch.GetInt()].parent=(int)ni;
    }
    if(d.HasMember("skins") && d["skins"].IsArray() && !d["skins"].Empty()) {
        const J& skin=d["skins"][0];
        if(skin.HasMember("joints") && skin["joints"].IsArray()) {
            for(auto& j:skin["joints"].GetArray()) if(j.IsInt())mJointNodes.push_back(j.GetInt());
            int ai=num(skin,"inverseBindMatrices",-1);
            std::vector<float> inv;
            if(ai>=0 && !getFloat(ai,16,inv)) return false;
            for(size_t i=0;i<mJointNodes.size();++i) {
                Mat4 mat=identity();
                if(inv.size()>=(i+1)*16)std::copy(inv.begin()+i*16,inv.begin()+(i+1)*16,mat.begin());
                mInverseBind.push_back(mat);
            }
        }
    }
    if(!mJointNodes.empty() && d.HasMember("animations") && d["animations"].IsArray() && !d["animations"].Empty()) {
        const J& anim=d["animations"][0];
        if(anim.HasMember("samplers") && anim["samplers"].IsArray() && anim.HasMember("channels") && anim["channels"].IsArray()) {
            const J& samps=anim["samplers"];
            for(auto& ch:anim["channels"].GetArray()) {
                int si=num(ch,"sampler",-1);
                if(si<0 || (size_t)si>=samps.Size() || !ch.HasMember("target"))continue;
                const J& tg=ch["target"];
                if(!tg.IsObject() || !tg.HasMember("path") || !tg["path"].IsString())continue;
                std::string path=tg["path"].GetString();
                int p=path=="translation"?0:path=="rotation"?1:path=="scale"?2:-1;
                if(p<0)continue;
                AnimTrack t; t.node=num(tg,"node",-1);t.path=p;
                if(t.node<0 || (size_t)t.node>=mBoneNodes.size())continue;
                const J& samp=samps[si];
                std::string mode=samp.HasMember("interpolation") && samp["interpolation"].IsString()?samp["interpolation"].GetString():"LINEAR";
                if(mode!="STEP" && mode!="LINEAR")continue;
                t.step=(mode=="STEP");
                if(!getFloat(num(samp,"input",-1),1,t.times) || !getFloat(num(samp,"output",-1),p==1?4:3,t.values))continue;
                if(t.times.empty() || t.values.size()!=t.times.size()*(p==1?4:3))continue;
                mAnimDuration=std::max(mAnimDuration,t.times.back());
                mAnimTracks.push_back(std::move(t));
            }
        }
    }
    if(d.HasMember("images") && d["images"].IsArray()) {
        const J& imgs=d["images"];
        if(imgs.Size()>64) return false;
        mPictures.resize(imgs.Size());
        for(rapidjson::SizeType i=0;i<imgs.Size();++i) {
            int vi=num(imgs[i],"bufferView",-1);
            if(vi<0 || (size_t)vi>=views.Size()) continue;
            const J& v=views[vi];
            size_t start=(size_t)num(v,"byteOffset"), len=(size_t)num(v,"byteLength");
            if(start<=bin.size() && len<=bin.size()-start)
                mPictures[i].encoded.assign(bin.begin()+start,bin.begin()+start+len);
        }
    }
    const J* materials=d.HasMember("materials") && d["materials"].IsArray()?&d["materials"]:nullptr;
    const J* samplers=d.HasMember("samplers") && d["samplers"].IsArray()?&d["samplers"]:nullptr;
    const J* textures=d.HasMember("textures") && d["textures"].IsArray()?&d["textures"]:nullptr;
    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    size_t totalTris=0;
    std::function<bool(int,const std::array<float,16>&,int)> visit;
    visit=[&](int ni,const std::array<float,16>& parent,int depth)->bool {
        if(depth>32 || ni<0 || (size_t)ni>=nodes.Size()) return false;
        const J& node=nodes[ni];
        auto mat=multiply(parent,nodeMatrix(node));
        int mi=num(node,"mesh",-1);
        if(mi>=0 && (size_t)mi<meshes.Size()) {
            const J& mesh=meshes[mi];
            if(!mesh.HasMember("primitives") || !mesh["primitives"].IsArray()) return false;
            for(auto& prim:mesh["primitives"].GetArray()) {
                if(num(prim,"mode",4)!=4 || !prim.HasMember("attributes") || !prim["attributes"].IsObject()) return false;
                const J& attr=prim["attributes"];
                if(!attr.HasMember("POSITION") || !attr["POSITION"].IsInt()) return false;
                std::vector<float> pos,nrm,uv;
                if(!getFloat(attr["POSITION"].GetInt(),3,pos)) return false;
                if(attr.HasMember("NORMAL") && !getFloat(attr["NORMAL"].GetInt(),3,nrm)) return false;
                if(attr.HasMember("TEXCOORD_0") && !getFloat(attr["TEXCOORD_0"].GetInt(),2,uv)) return false;

                // GLB_VERTEX_COLOR_V1: glTF COLOR_0 may be RGB/RGBA float or normalized integers.
                std::vector<float> vertexColors;
                if(attr.HasMember("COLOR_0") && attr["COLOR_0"].IsInt()) {
                    int ci=attr["COLOR_0"].GetInt();
                    if(ci<0 || (size_t)ci>=access.Size()) return false;
                    const J& ca=access[ci];
                    int ct=num(ca,"componentType");
                    int nc=0;
                    if(ca.HasMember("type") && ca["type"].IsString()) {
                        std::string kind=ca["type"].GetString();
                        nc=kind=="VEC4"?4:kind=="VEC3"?3:0;
                    }
                    if(!nc || (ct!=5126 && ct!=5121 && ct!=5123))
                        return false;
                    size_t count=0,stride=0;
                    const unsigned char* p=nullptr;
                    if(!span(ci,nc,ct,count,stride,p) ||
                       count!=pos.size()/3) return false;
                    vertexColors.assign(count*4,1.0f);
                    for(size_t i=0;i<count;++i)
                        for(int k=0;k<nc;++k) {
                            float value=1.0f;
                            const unsigned char* q=p+i*stride+
                                k*(ct==5126?4:ct==5123?2:1);
                            if(ct==5126) std::memcpy(&value,q,4);
                            else if(ct==5123)
                                value=float(q[0] | (q[1]<<8))/65535.0f;
                            else value=float(q[0])/255.0f;
                            vertexColors[i*4+k]=value;
                        }
                }

                std::vector<uint32_t> ids;
                if(prim.HasMember("indices")) {
                    if(!getIndices(num(prim,"indices",-1),ids)) return false;
                } else { ids.resize(pos.size()/3); for(size_t i=0;i<ids.size();++i)ids[i]=i; }
                if(ids.size()%3 || totalTris+ids.size()/3>50000) return false;
                totalTris+=ids.size()/3;
                Part part;
                part.skin=num(node,"skin",-1);
                std::vector<uint32_t> joint4;
                std::vector<float> weights4;
                if(part.skin==0 && !mJointNodes.empty() && attr.HasMember("JOINTS_0") && attr.HasMember("WEIGHTS_0")) {
                    int ja=num(attr,"JOINTS_0",-1),wa=num(attr,"WEIGHTS_0",-1);
                    if(ja>=0 && (size_t)ja<access.Size()) {
                        int ct=num(access[ja],"componentType");
                        size_t count=0,stride=0;const unsigned char* ptr=nullptr;
                        if((ct==5121 || ct==5123) && span(ja,4,ct,count,stride,ptr)) {
                            joint4.resize(count*4);
                            for(size_t ii=0;ii<count;++ii)for(int k=0;k<4;++k)
                                joint4[ii*4+k]=ct==5121?ptr[ii*stride+k]:(uint32_t)(ptr[ii*stride+k*2]|ptr[ii*stride+k*2+1]<<8);
                        }
                    }
                    getFloat(wa,4,weights4);
                    if(joint4.size()!=pos.size()/3*4 || weights4.size()!=pos.size()/3*4)part.skin=-1;
                } else part.skin=-1;
                int material=num(prim,"material",-1);
                if(materials && material>=0 && (size_t)material<materials->Size()) {
                    const J& matJ=(*materials)[material];
                    part.doubleSided=matJ.HasMember("doubleSided") &&
                        matJ["doubleSided"].IsBool() &&
                        matJ["doubleSided"].GetBool();
                    if(matJ.HasMember("alphaMode") &&
                       matJ["alphaMode"].IsString()) {
                        const char* mode=matJ["alphaMode"].GetString();
                        part.blend=std::strcmp(mode,"BLEND")==0;
                        part.alphaMask=std::strcmp(mode,"MASK")==0;
                    }
                    if(part.alphaMask && matJ.HasMember("alphaCutoff") &&
                       matJ["alphaCutoff"].IsNumber())
                        part.alphaCutoff=std::max(0.0f,std::min(1.0f,
                            matJ["alphaCutoff"].GetFloat()));
                    if(matJ.HasMember("pbrMetallicRoughness")) {
                        const J& pbr=matJ["pbrMetallicRoughness"];
                        if(pbr.HasMember("baseColorFactor") && pbr["baseColorFactor"].IsArray() && pbr["baseColorFactor"].Size()==4)
                            for(int k=0;k<4;++k)part.color[k]=pbr["baseColorFactor"][k].GetFloat();
                        if(textures && pbr.HasMember("baseColorTexture")) {
                            int tx=num(pbr["baseColorTexture"],"index",-1);
                            if(tx>=0 && (size_t)tx<textures->Size()) {
                                const J& texture=(*textures)[tx];
                                part.image=num(texture,"source",-1);
                                int si=num(texture,"sampler",-1);
                                if(samplers && si>=0 && (size_t)si<samplers->Size()) {
                                    part.wrapS=num((*samplers)[si],"wrapS",10497);
                                    part.wrapT=num((*samplers)[si],"wrapT",10497);
                                }
                            }
                        }
                    }
                }
                for(uint32_t id:ids) {
                    if(id>=pos.size()/3) return false;
                    Vertex vertex={};
                    if(part.skin==0) for(int j=0;j<4;++j) {
                        vertex.joint[j]=(unsigned short)joint4[id*4+j];
                        vertex.weight[j]=weights4[id*4+j];
                    }
                    for(int j=0;j<3;++j)vertex.bindPos[j]=pos[id*3+j];
                    const float* p=&pos[id*3];
                    for(int k=0;k<3;++k) {
                        vertex.pos[k]=mat[k]*p[0]+mat[4+k]*p[1]+mat[8+k]*p[2]+mat[12+k];
                        low[k]=std::min(low[k],vertex.pos[k]);high[k]=std::max(high[k],vertex.pos[k]);
                    }
                    if(nrm.size()>id*3+2) {
                        const float* n=&nrm[id*3];
                        for(int k=0;k<3;++k)vertex.normal[k]=mat[k]*n[0]+mat[4+k]*n[1]+mat[8+k]*n[2];
                    } else vertex.normal[1]=1;
                    float nl=std::sqrt(vertex.normal[0]*vertex.normal[0]+vertex.normal[1]*vertex.normal[1]+vertex.normal[2]*vertex.normal[2]);
                    if(nl>0.0001f) for(int k=0;k<3;++k)vertex.normal[k]/=nl;
                    if(uv.size()>id*2+1) {vertex.uv[0]=uv[id*2];vertex.uv[1]=1.0f-uv[id*2+1];}
                    for(int k=0;k<4;++k) {
                        float linear=part.color[k]*
                            (vertexColors.empty()?1.0f:vertexColors[id*4+k]);
                        if(k<3) {
                            linear=std::max(0.0f,std::min(1.0f,linear));
                            vertex.color[k]=linear<=0.0031308f
                                ?12.92f*linear
                                :1.055f*std::pow(linear,1.0f/2.4f)-0.055f;
                        } else vertex.color[k]=linear;
                    }
                    part.verts.push_back(vertex);
                }
                mParts.push_back(std::move(part));
            }
        }
        if(node.HasMember("children") && node["children"].IsArray())
            for(auto& c:node["children"].GetArray()) if(!c.IsInt() || !visit(c.GetInt(),mat,depth+1)) return false;
        return true;
    };
    if(!d.HasMember("scenes") || !d["scenes"].IsArray() || d["scenes"].Empty()) return false;
    int si=num(d,"scene",0);
    if(si<0 || (size_t)si>=d["scenes"].Size()) return false;
    const J& scene=d["scenes"][si];
    if(!scene.HasMember("nodes") || !scene["nodes"].IsArray()) return false;
    for(auto& n:scene["nodes"].GetArray()) if(!n.IsInt() || !visit(n.GetInt(),identity(),0)) return false;
    if(mParts.empty() || !std::isfinite(low[0])) return false;
    for(int k=0;k<3;++k) mCenter[k]=(low[k]+high[k])*0.5f;
    mExtent=std::max({high[0]-low[0],high[1]-low[1],high[2]-low[2]});
    mBottomY=low[1]; // GLB_BOTTOM_ANCHOR_V1
    return mExtent>0.00001f && std::isfinite(mExtent);
}
void GlbPreviewComponent::updateAnimation() {
    if(mJointNodes.empty() || mInverseBind.size()!=mJointNodes.size())return;
    std::vector<BoneNode> pose=mBoneNodes;
    for(const auto& track:mAnimTracks) {
        if(track.times.empty())continue;
        size_t i=0;
        while(i+1<track.times.size() && track.times[i+1]<=mAnimTime)++i;
        size_t j=std::min(i+1,track.times.size()-1);
        float blend=0;
        if(!track.step && j!=i && track.times[j]>track.times[i])
            blend=(mAnimTime-track.times[i])/(track.times[j]-track.times[i]);
        int comps=track.path==1?4:3;
        float v[4]={0,0,0,1};
        if(track.path==1 && j!=i && blend>0) {
            float dot=0;for(int k=0;k<4;++k)dot+=track.values[i*4+k]*track.values[j*4+k];
            float sign=dot<0?-1.f:1.f;
            for(int k=0;k<4;++k)v[k]=track.values[i*4+k]*(1-blend)+sign*track.values[j*4+k]*blend;
            float norm=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]+v[3]*v[3]);
            if(norm>0.00001f)for(float& x:v)x/=norm;
        } else {
            for(int k=0;k<comps;++k)v[k]=track.values[i*comps+k]*(1-blend)+track.values[j*comps+k]*blend;
        }
        float* dst=track.path==0?pose[track.node].t:track.path==1?pose[track.node].r:pose[track.node].s;
        for(int k=0;k<comps;++k)dst[k]=v[k];
        pose[track.node].hasMatrix=false;
    }
    auto trs=[](const BoneNode& n)->Mat4 {
        if(n.hasMatrix)return n.matrix;
        float x=n.r[0],y=n.r[1],z=n.r[2],w=n.r[3];
        Mat4 m=identity();
        m[0]=(1-2*(y*y+z*z))*n.s[0]; m[1]=2*(x*y+z*w)*n.s[0];m[2]=2*(x*z-y*w)*n.s[0];
        m[4]=2*(x*y-z*w)*n.s[1];m[5]=(1-2*(x*x+z*z))*n.s[1];m[6]=2*(y*z+x*w)*n.s[1];
        m[8]=2*(x*z+y*w)*n.s[2];m[9]=2*(y*z-x*w)*n.s[2];m[10]=(1-2*(x*x+y*y))*n.s[2];
        for(int k=0;k<3;++k)m[12+k]=n.t[k];
        return m;
    };
    std::vector<Mat4> globals(pose.size(),identity());
    std::vector<char> state(pose.size(),0);
    std::function<void(size_t)> calc=[&](size_t n) {
        if(state[n]==2 || state[n]==1)return;
        state[n]=1;Mat4 local=trs(pose[n]);int parent=pose[n].parent;
        if(parent>=0 && (size_t)parent<pose.size()) {calc((size_t)parent);globals[n]=multiply(globals[parent],local);}
        else globals[n]=local;
        state[n]=2;
    };
    for(size_t n=0;n<pose.size();++n)calc(n);
    std::vector<Mat4> jointMatrices(mJointNodes.size(),identity());
    for(size_t j=0;j<mJointNodes.size();++j) {
        int node=mJointNodes[j];
        if(node>=0 && (size_t)node<globals.size())jointMatrices[j]=multiply(globals[node],mInverseBind[j]);
    }
    for(auto& part:mParts) {
        if(part.skin!=0)continue;
        for(auto& v:part.verts) {
            float r[3]={0,0,0};float sum=0;
            for(int k=0;k<4;++k) {
                float w=v.weight[k];int joint=v.joint[k];
                if(w<=0 || joint<0 || (size_t)joint>=jointMatrices.size())continue;
                const Mat4& m=jointMatrices[joint];
                for(int a=0;a<3;++a)r[a]+=w*(m[a]*v.bindPos[0]+m[4+a]*v.bindPos[1]+m[8+a]*v.bindPos[2]+m[12+a]);
                sum+=w;
            }
            if(sum>0.00001f)for(int a=0;a<3;++a)v.pos[a]=r[a]/sum;
        }
    }
}
bool GlbPreviewComponent::uploadTextures() {
    for(auto& pic:mPictures) {
        if(pic.texture || pic.encoded.empty()) continue;
        FIMEMORY* mem=FreeImage_OpenMemory(pic.encoded.data(),(DWORD)pic.encoded.size());
        if(!mem) continue;
        FREE_IMAGE_FORMAT fmt=FreeImage_GetFileTypeFromMemory(mem,0);
        FIBITMAP* bmp=fmt!=FIF_UNKNOWN?FreeImage_LoadFromMemory(fmt,mem,0):nullptr;
        if(bmp) {
            FIBITMAP* rgba=FreeImage_ConvertTo32Bits(bmp);
            FreeImage_Unload(bmp);
            if(rgba) {
                unsigned w=FreeImage_GetWidth(rgba),h=FreeImage_GetHeight(rgba);
                if(w && h && w<=2048 && h<=2048) {
                    std::vector<unsigned char> pixels((size_t)w*h*4);
                    for(unsigned y=0;y<h;++y) {
                        BYTE* scan=FreeImage_GetScanLine(rgba,y);
                        for(unsigned x=0;x<w;++x) {
                            size_t i=((size_t)y*w+x)*4;
                            pixels[i+0]=scan[x*4+FI_RGBA_RED];
                            pixels[i+1]=scan[x*4+FI_RGBA_GREEN];
                            pixels[i+2]=scan[x*4+FI_RGBA_BLUE];
                            pixels[i+3]=scan[x*4+FI_RGBA_ALPHA];
                        }
                    }
                    GLuint id=0; glGenTextures(1,&id); glBindTexture(GL_TEXTURE_2D,id);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    pic.texture=id;
                }
                FreeImage_Unload(rgba);
            }
        }
        FreeImage_CloseMemory(mem);
    }
    return true;
}
void GlbPreviewComponent::render(const Transform4x4f& parentTrans) {
    if(!mValid || !isVisible() || mParts.empty()) return;


    static bool firstFrame=true;
    if(firstFrame) { std::fprintf(stderr,"[GLBDBG] render chamado: tamanho=%gx%g partes=%zu\n",double(mSize.x()),double(mSize.y()),mParts.size()); std::fflush(stderr); }
    Transform4x4f tr=parentTrans*getTransform();
    int x=std::lround(tr.translation().x()), y=std::lround(tr.translation().y());
    int w=std::lround(mSize.x()),h=std::lround(mSize.y());
    int sw=Renderer::getWindowWidth(), sh=Renderer::getWindowHeight();
    if(w<1 || h<1 || sw<1 || sh<1) return;
    // Screen coords are top-left; GL coords are bottom-left.
    int vx=std::max(0,x), vy=std::max(0,sh-y-h), vw=std::min(sw,vx+w)-vx, vh=std::min(sh,vy+h)-vy;
    if(vw<1 || vh<1) { if(firstFrame) std::fprintf(stderr,"[GLBDBG] viewport invalido: %d,%d %dx%d\n",vx,vy,vw,vh); return; }
    if(firstFrame) { std::fprintf(stderr,"[GLBDBG] viewport: %d,%d %dx%d tela=%dx%d\n",vx,vy,vw,vh,sw,sh); std::fflush(stderr); }
    GLint oldView[4],oldScissor[4],oldMode,oldTex;
    glGetIntegerv(GL_VIEWPORT,oldView);glGetIntegerv(GL_SCISSOR_BOX,oldScissor);
    glGetIntegerv(GL_MATRIX_MODE,&oldMode);glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTex);
    GLboolean wasScissor=glIsEnabled(GL_SCISSOR_TEST),wasDepth=glIsEnabled(GL_DEPTH_TEST),wasTex=glIsEnabled(GL_TEXTURE_2D);
    GLboolean wasBlend=glIsEnabled(GL_BLEND),wasCull=glIsEnabled(GL_CULL_FACE),wasLighting=glIsEnabled(GL_LIGHTING);
    GLboolean depthMask;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    uploadTextures();
    GLint oldCullMode=GL_BACK,oldFrontFace=GL_CCW;
    glGetIntegerv(GL_CULL_FACE_MODE,&oldCullMode);
    glGetIntegerv(GL_FRONT_FACE,&oldFrontFace);
    glViewport(vx,vy,vw,vh); glScissor(vx,vy,vw,vh); glEnable(GL_SCISSOR_TEST);

    glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL); // GLB_DEPTH_FIX_V1
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
    float ar=float(w)/float(h);
    glOrthof(-2.08f*ar,2.08f*ar,-2.08f,2.08f,1.0f,20.0f);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    // GLB_ORIGIN_FLOOR_V1
    // Posiciona Y=0 do GLB junto a base do viewport.
    const float floorY = -0.65f * 3.2f * 0.95f;
    glTranslatef(0.0f, floorY, -3.2f); // GLB_BOTTOM_ANCHOR_V2
    glRotatef(0,1,0,0);
    glRotatef(mAngle,0,1,0);
    float scale=(2.0f/mExtent)*mZoom; // GLB_XML_ZOOM_V1
    glScalef(scale,scale,scale);
    glTranslatef(0.0f, 0.0f, 0.0f); // GLB_ORIGIN_ZERO_V1 // GLB_BOTTOM_ANCHOR_V1
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    GLboolean oldAlphaTest=glIsEnabled(GL_ALPHA_TEST);
    GLint oldAlphaFunc=GL_ALWAYS;
    GLfloat oldAlphaRef=0.0f;
    glGetIntegerv(GL_ALPHA_TEST_FUNC,&oldAlphaFunc);
    glGetFloatv(GL_ALPHA_TEST_REF,&oldAlphaRef);
    GLboolean previousColorArray=glIsEnabled(GL_COLOR_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    for(const auto& part:mParts) {
        if(part.verts.empty()) continue;
        if(part.doubleSided) glDisable(GL_CULL_FACE);
        else glEnable(GL_CULL_FACE);
        const auto& v=part.verts[0];
        GLuint tex=(part.image>=0 && (size_t)part.image<mPictures.size())?mPictures[part.image].texture:0;
        if(tex) {
            glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,tex);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,part.wrapS);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,part.wrapT);
        }
        else {glDisable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,0);}
        if(part.alphaMask) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER,part.alphaCutoff);
        } else glDisable(GL_ALPHA_TEST);
        if(part.blend || (!part.alphaMask && part.color[3]<0.999f)) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        } else glDisable(GL_BLEND);
        glColor4f(1,1,1,1);
        glColorPointer(4,GL_FLOAT,sizeof(Vertex),v.color);
        glVertexPointer(3,GL_FLOAT,sizeof(Vertex),v.pos);
        glNormalPointer(GL_FLOAT,sizeof(Vertex),v.normal);
        glTexCoordPointer(2,GL_FLOAT,sizeof(Vertex),v.uv);
        glDrawArrays(GL_TRIANGLES,0,(GLsizei)part.verts.size());
    }
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_NORMAL_ARRAY);glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if(!previousColorArray)glDisableClientState(GL_COLOR_ARRAY);
    glAlphaFunc(oldAlphaFunc,oldAlphaRef);
    if(oldAlphaTest)glEnable(GL_ALPHA_TEST);
    else glDisable(GL_ALPHA_TEST);
    glColor4f(1,1,1,1);
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();
    glMatrixMode(oldMode);
    glViewport(oldView[0],oldView[1],oldView[2],oldView[3]);
    glScissor(oldScissor[0],oldScissor[1],oldScissor[2],oldScissor[3]);
    if(wasScissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    if(wasDepth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(wasTex)glEnable(GL_TEXTURE_2D);else glDisable(GL_TEXTURE_2D);
    if(wasBlend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    glCullFace(oldCullMode);
    glFrontFace(oldFrontFace);
    if(wasCull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(wasLighting)glEnable(GL_LIGHTING);else glDisable(GL_LIGHTING);
    glDepthMask(depthMask);glBindTexture(GL_TEXTURE_2D,oldTex);
    Renderer::setMatrix(tr);




    if(firstFrame) { std::fprintf(stderr,"[GLBDBG] GL erro apos desenho=0x%x\n",unsigned(glGetError())); std::fflush(stderr); firstFrame=false; }
}
