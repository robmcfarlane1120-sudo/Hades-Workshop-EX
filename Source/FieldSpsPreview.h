#pragma once
#include "Fields.h"
#include "Database_Steam.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

// Detached CPU preview of MDC's all-scenario background SPS and screen-space rain.
// No game VM, live VRAM, actor attachment, or editable field data is modified.
class FieldSpsPreview {
    using Bytes = std::vector<uint8_t>;
    struct Bundle { UnityArchiveMetaData meta; UnityArchiveAssetBundle index; std::wstring path; };
    struct Primitive { int x,y,u,v,r,g,b; };
    struct Asset { int w,h; std::vector<std::vector<Primitive>> frames; std::vector<uint32_t> texture; };
    struct Instance {
        std::shared_ptr<Asset> asset;
        int slot=0, rate=16, scale=4096, fade=128, blend=15, initial=0;
        double x=0,y=0,z=0, ox=0,oy=0,oz=0, rx=0,ry=0,rz=0;
        bool positioned=false, ground=false;
        std::array<int,6> groundData{};
    };
    std::map<std::wstring,std::shared_ptr<Bundle>> bundles;
    std::map<int,std::vector<uint16_t>> vrams;
    std::map<std::pair<int,int>,std::shared_ptr<Asset>> assets;
    std::vector<Instance> instances;
    wxString gameRoot;
    FieldTilesCameraDataStruct camera{};
    double sceneX=0,sceneY=0,logicalWidth=1;
    int rainStrength=0,rainSpeed=0, skipped=0;
    static void Check(bool ok) { if (!ok) throw std::runtime_error("Invalid or missing SPS preview data"); }
    static unsigned Read(const Bytes& data, size_t offset, int size) {
        Check(offset <= data.size() && size <= data.size()-offset);
        unsigned value=0; for (int i=0;i<size;++i) value |= (unsigned)data[offset+i] << (8*i); return value;
    }
    static const SteamFieldDictionary& Entry(int id) {
        for (const auto& entry : SteamFieldScript) if (entry.script_id==id) return entry;
        throw std::runtime_error("Unknown SPS origin field");
    }
    static std::vector<int> Numbers(std::string text) {
        std::replace(text.begin(),text.end(),',',' '); std::istringstream in(text);
        std::vector<int> out; int n; while(in>>n) { Check(out.size()<16); out.push_back(n); } return out;
    }
    static Bytes ReadFile(const wxString& path) {
        std::ifstream in(path.ToStdWstring(),std::ios::binary|std::ios::ate);
        if (!in) return {};
        auto length=in.tellg(); Check(length>0 && length<=16777216);
        Bytes result((size_t)length); in.seekg(0); in.read((char*)result.data(),result.size()); Check(in.good()); return result;
    }
    Bytes Resource(int origin, const std::string& name) {
        const auto& entry=Entry(origin); Check(entry.file_id>=1 && entry.file_id<=9);
        const std::string map=wxString::FromUTF8(entry.background_name).Lower().ToStdString();
        const std::string key="assets/resources/fieldmaps/"+map+"/"+name+".bytes";
        for (const wxString& prefix : {wxString("MoguriMain\\StreamingAssets\\"),wxString("StreamingAssets\\")}) {
            wxString root=gameRoot+prefix;
            Bytes loose=ReadFile(root+wxString::FromUTF8(key)); if (!loose.empty()) return loose;
            wxString path=root+wxString::Format("p0data%d.bin",entry.file_id+10);
            if (!wxFileExists(path)) continue;
            auto& cached=bundles[path.ToStdWstring()];
            if (!cached) {
                auto candidate=std::make_shared<Bundle>(); candidate->path=path.ToStdWstring();
                std::fstream stream(candidate->path,std::ios::in|std::ios::binary);
                Check(candidate->meta.Load(stream,(UnityArchiveFile)(UNITY_ARCHIVE_DATA11+entry.file_id-1))==0);
                auto offset=candidate->meta.GetFileOffset("",142); Check(offset!=0);
                stream.seekg(offset); candidate->index.Load(stream); Check(stream.good()); cached=candidate;
            }
            int index=cached->meta.GetFileIndexByInfo(cached->index.GetFileInfo(key),49);
            if (index<0) continue;
            auto size=cached->meta.GetFileSizeByIndex(index); Check(size>0 && size<=16777216);
            Bytes result(size); std::ifstream stream(cached->path,std::ios::binary);
            stream.seekg(cached->meta.GetFileOffsetByIndex(index)); stream.read((char*)result.data(),size); Check(stream.good()); return result;
        }
        throw std::runtime_error("SPS resource not installed");
    }
    const std::vector<uint16_t>& Vram(int origin) {
        auto found=vrams.find(origin); if(found!=vrams.end()) return found->second;
        Bytes tcb=Resource(origin,"spt.tcb");
        std::vector<uint16_t> memory(1024*512,0);
        auto block=[&](size_t header,size_t pixels) {
            int x=(int16_t)Read(tcb,header,2), y=(int16_t)Read(tcb,header+2,2);
            int w=(int16_t)Read(tcb,header+4,2),h=(int16_t)Read(tcb,header+6,2);
            Check(x>=0&&y>=0&&w>0&&h>0&&x+w<=1024&&y+h<=512);
            Check(pixels<=tcb.size() && (size_t)w*h*2<=tcb.size()-pixels);
            for(int row=0;row<h;++row) for(int col=0;col<w;++col)
                memory[(y+row)*1024+x+col]=(uint16_t)Read(tcb,pixels+2*(row*w+col),2);
            return (size_t)w*h*2;
        };
        size_t second=Read(tcb,0,4),first=Read(tcb,4,4); unsigned count=Read(tcb,8,4); Check(count<=4096);
        for(unsigned i=0;i<count;++i) first+=block(first,first+8)+8;
        size_t pixel=Read(tcb,second,4); count=Read(tcb,second+4,4); Check(count<=4096); second+=8;
        for(unsigned i=0;i<count;++i) { pixel+=block(second,pixel); second+=8; }
        return vrams.emplace(origin,std::move(memory)).first->second;
    }
    std::shared_ptr<Asset> LoadAsset(int origin,int id) {
        auto key=std::make_pair(origin,id); auto found=assets.find(key); if(found!=assets.end()) return found->second;
        Check(assets.size()<128); Bytes bin=Resource(origin,std::to_string(id)+".sps");
        int frames=Read(bin,0,2)&32767; Check(frames>0&&frames<=4096);
        auto asset=std::make_shared<Asset>(); asset->h=((int)Read(bin,6,1)-1)*2; asset->w=((int)Read(bin,7,1)-1)*2;
        Check(asset->w>0&&asset->h>0&&asset->w<=508&&asset->h<=508);
        size_t table=8+frames*2,uv=table+2,rgb=uv+Read(bin,table,2)*2+2;
        size_t totalPrimitives=0;
        for(int frame=0;frame<frames;++frame) {
            size_t offset=Read(bin,8+frame*2,2); int count=Read(bin,offset++,1);
            totalPrimitives+=count; Check(totalPrimitives<=131072);
            std::vector<Primitive> list;
            for(int p=0;p<count;++p) {
                int x=(int8_t)Read(bin,offset++,1)*4,y=(int8_t)Read(bin,offset++,1)*4,packed=Read(bin,offset++,1);
                size_t tc=uv+(packed&15)*2,colour=rgb+(packed>>4)*4;
                int u=Read(bin,tc,1),v=Read(bin,tc+1,1);
                Check(u+asset->w/2<=256&&v+asset->h/2<=256);
                list.push_back({x,y,u,v,(int)Read(bin,colour,1),(int)Read(bin,colour+1,1),(int)Read(bin,colour+2,1)});
            }
            asset->frames.push_back(std::move(list));
        }
        const auto& memory=Vram(origin); int page=Read(bin,2,2),clut=Read(bin,4,2),mode=(page>>7)&3;
        Check(mode<=2); int px=(page&15)*64,py=((page>>4)&1)*256,cx=(clut&63)*16,cy=(clut>>6)&511;
        asset->texture.resize(256*256);
        for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
            int tx=px+(mode==0?x/4:mode==1?x/2:x); Check(tx<1024&&py+y<512);
            unsigned colour=memory[(py+y)*1024+tx];
            if(mode<2) { int index=mode==0?(colour>>((x%4)*4))&15:(colour>>((x%2)*8))&255; Check(cx+index<1024); colour=memory[cy*1024+cx+index]; }
            asset->texture[y*256+x]=colour==0?0:0xff000000|((colour&31)<<19)|(((colour>>5)&31)<<11)|(((colour>>10)&31)<<3);
        }
        assets[key]=asset; return asset;
    }
    template<class Visitor> void Rows(const wxString& folder,const wxString& file,int field,Visitor visitor) {
        std::ifstream in((folder+file).ToStdWstring()); std::string line;
        while(std::getline(in,line)) {
            if(line.size()>32768) continue;
            auto tab=line.find('\t'); if(tab==std::string::npos) continue;
            if(line.substr(0,tab)!=std::to_string(field)) continue;
            try { visitor(line.substr(tab+1)); } catch(const std::exception&) { ++skipped; }
        }
    }
    static double Fraction(double n) { return n-std::floor(n); }
    static void Blend(uint8_t* dest,uint32_t source,int r,int g,int b,int fade,int mode) {
        if(!(source>>24)) return;
        const int colors[]={r,g,b};
        for(int c=0;c<3;++c) {
            int texture=(source>>(16-8*c))&255;
            int value=std::min(255,texture*colors[c]*fade/(256*127));
            if(mode==0) dest[c]=(dest[c]+value)/2;
            else if(mode==1) dest[c]=std::min(255,dest[c]+value);
            else if(mode==2) dest[c]=std::max(0,dest[c]-value);
            else if(mode==3) dest[c]=std::min(255,dest[c]+value/4);
            else dest[c]=value;
        }
    }
public:
    bool HasEffects() const { return !instances.empty()||rainStrength>0; }
    size_t Count() const { return instances.size(); }
    int Skipped() const { return skipped; }
    void Load(FieldTilesDataStruct& field,ConfigurationSet& config,int id,int cam,const wxString& referenceFolder=wxEmptyString) {
        Check(cam>=0&&cam<(int)field.camera.size()); camera=field.camera[cam]; sceneX=field.pos_x;sceneY=field.pos_y; logicalWidth=camera.width*16.0/std::max(1,(int)field.parent->tile_size);
        wxFileName root=wxFileName::DirName(wxString::FromUTF8(config.steam_dir_assets));root.RemoveLastDir();gameRoot=root.GetPathWithSep();
        wxString refs=referenceFolder;
        if(refs.empty()) refs=wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPathWithSep();
        if(!wxFileExists(refs+"FieldViewerSpsPresets.tsv")) refs=gameRoot+"Memoria Dev Console\\FF9_Data\\FieldViewer\\";
        Check(wxFileExists(refs+"FieldViewerSpsPresets.tsv"));
        std::map<int,std::array<int,6>> ground;
        Rows(refs,"FieldViewerSpsGround.tsv",id,[&](std::string row) { auto n=Numbers(row);Check(n.size()==6&&n[4]>=0&&n[4]<=20&&n[5]>=0&&n[5]<=20);std::array<int,6> a;std::copy(n.begin(),n.end(),a.begin());ground[n[0]]=a; });
        Rows(refs,"FieldViewerWeather.tsv",id,[&](std::string row) {auto n=Numbers(row);Check(n.size()==2);if(n[0]>rainStrength){rainStrength=std::min(255,n[0]);rainSpeed=std::max(0,std::min(255,n[1]));}});
        Rows(refs,"FieldViewerSpsPresets.tsv",id,[&](std::string row) {
            if(instances.size()>=256) return;
            std::istringstream commands(row);std::string command;std::getline(commands,command,';');auto load=Numbers(command);Check(load.size()==4);
            // Prototypes/world effects and actor-attached effects are not background SPS.
            if(load[3]!=0) return;
            Instance effect;effect.slot=load[1];bool actor=false;
            while(std::getline(commands,command,';')) {
                auto n=Numbers(command);Check(n.size()==4);int op=n[0];
                if(op==150||op==151) actor=true;
                else if(op==135){effect.x=(int16_t)n[1];effect.y=(int16_t)n[2];effect.z=(int16_t)n[3];effect.positioned=n[1]||n[2]||n[3];}
                else if(op==140){effect.rx=n[1]*6.283185307/4096;effect.ry=n[2]*6.283185307/4096;effect.rz=n[3]*6.283185307/4096;}
                else if(op==145)effect.scale=n[1]>0?n[1]:4096;
                else if(op==155)effect.fade=n[1]>0?std::min(255,n[1]):128;
                else if(op==156)effect.blend=n[1];
                else if(op==160)effect.rate=n[1]?n[1]:16;
                else if(op==161)effect.initial=n[1]*16;
                else if(op==165){effect.ox=(int16_t)n[1];effect.oy=(int16_t)n[2];effect.oz=(int16_t)n[3];}
            }
            if(actor) return;
            auto g=ground.find(effect.slot);if(g!=ground.end()){effect.ground=true;effect.groundData=g->second;effect.positioned=true;effect.initial+=effect.slot*112;}
            effect.asset=LoadAsset(load[0],load[2]);instances.push_back(std::move(effect));
        });
        // Decoded assets own their texture pixels. Release archive/VRAM working storage.
        bundles.clear();vrams.clear();assets.clear();
    }
    wxBitmap Render(const wxBitmap& background,double seconds) const {
        wxImage image=background.ConvertToImage(); Check(image.IsOk());
        const int width=image.GetWidth(),height=image.GetHeight();auto* pixels=image.GetData();
        double factor=4; // Moguri preview uses 64 pixels per 16-pixel field tile.
        // Fallback HWS backgrounds can have another resolution.
        if(logicalWidth>0) factor=width/logicalWidth;
        size_t pixelBudget=8000000;
        for(size_t instanceIndex=0;instanceIndex<instances.size();++instanceIndex) {
            const auto& e=instances[instanceIndex];const auto& asset=*e.asset;
            long long raw=e.initial+(long long)(seconds*30)*e.rate,period=(long long)asset.frames.size()*16;
            int frame=(int)(((raw%period+period)%period)/16);
            double x=e.x,y=e.y,z=e.z;
            if(e.ground) {
                const auto& g=e.groundData;int cycle=(int)((seconds*30*std::max(1,std::abs(e.rate))+e.slot*112)/period);
                uint32_t seed=(uint32_t)g[0]*747796405u+(uint32_t)cycle*2891336453u+277803737u;
                seed^=seed>>16;seed*=2246822519u;seed^=seed>>13;
                double angle=(seed&255)*(6.283185307/256),radius=(seed>>8)&255;
                x=(int16_t)g[1]+std::sin(angle)*radius*4096/(1<<g[4]); y=(int16_t)g[2];z=(int16_t)g[3]+std::cos(angle)*radius*4096/(1<<g[5]);
            }
            x+=e.ox;y+=e.oy;z+=e.oz;
            const auto& m=camera.matrix;
            double cx=(m[0]*x+m[1]*y+m[2]*z)/4096+camera.offset_x;
            double cy=(m[3]*x+m[4]*y+m[5]*z)/4096+camera.offset_y;
            double depth=(m[6]*x+m[7]*y+m[8]*z)/4096+camera.offset_z;
            double px=(cx*camera.distance/std::max(1.0,depth)+camera.offset_centerx+camera.offset_width/2.0-sceneX-camera.pos_x)*factor;
            double py=(cy*camera.distance/std::max(1.0,depth)+camera.offset_centery+camera.offset_height/2.0-sceneY-camera.pos_y)*factor;
            if(!e.positioned&&(depth<=1||px<0||px>width||py<0||py>height)) {
                px=width*(.15+(instanceIndex*37%101)/100.0*.7);py=height*(.2+(instanceIndex*61%101)/100.0*.6);
                depth=std::max(1,(int)camera.distance)*8.0;
            }
            if(depth<=1)continue;
            double scale=e.scale/4096.0*camera.distance/depth*factor;
            if(!std::isfinite(scale)||scale<=0||scale>100)continue;
            double ca=std::cos(e.rz),sa=std::sin(e.rz),cxr=std::cos(e.rx),syr=std::sin(e.ry),sxr=std::sin(e.rx),cyr=std::cos(e.ry);
            double a=ca*cyr*scale,b=(ca*syr*sxr-sa*cxr)*scale,c=sa*cyr*scale,d=(sa*syr*sxr+ca*cxr)*scale,det=a*d-b*c;
            if(std::abs(det)<.00001)continue;
            for(const auto& p:asset.frames[frame]) {
                double centerX=px+a*p.x+b*p.y,centerY=py+c*p.x+d*p.y;
                double dx=std::abs(a*asset.w)+std::abs(b*asset.h),dy=std::abs(c*asset.w)+std::abs(d*asset.h);
                int left=std::max(0,(int)std::floor(centerX-dx)),right=std::min(width-1,(int)std::ceil(centerX+dx));
                int top=std::max(0,(int)std::floor(centerY-dy)),bottom=std::min(height-1,(int)std::ceil(centerY+dy));
                size_t area=(size_t)std::max(0,right-left+1)*std::max(0,bottom-top+1);
                Check(area<=pixelBudget);pixelBudget-=area;
                for(int sy=top;sy<=bottom;++sy)for(int sx=left;sx<=right;++sx){
                    double lx=(d*(sx-centerX)-b*(sy-centerY))/det,ly=(-c*(sx-centerX)+a*(sy-centerY))/det;
                    if(lx < -asset.w||lx>=asset.w||ly < -asset.h||ly>=asset.h)continue;
                    int u=p.u+(int)((lx+asset.w)/4),v=p.v+(int)((ly+asset.h)/4);
                    Blend(pixels+3*((size_t)sy*width+sx),asset.texture[v*256+u],p.r,p.g,p.b,e.fade,e.blend);
                }
            }
        }
        // MDC's weather is an approximate screen-space effect, not an SPS asset.
        for(int i=0;i<std::min(222,rainStrength*4);++i) {
            double phase=Fraction(i*.618034+seconds*std::max(1,rainSpeed)/70.0);
            int x=(int)(Fraction(i*.414214-phase*.08)*width),y=(int)(phase*height);
            int length=std::max(2,(int)((9+rainSpeed*.2)*height/960.0));
            for(int k=0;k<length&&y+k<height;++k) {
                auto* p=pixels+3*((size_t)(y+k)*width+x);p[0]=(p[0]*55+166*45)/100;p[1]=(p[1]*55+204*45)/100;p[2]=(p[2]*55+255*45)/100;
            }
        }
        return wxBitmap(image);
    }
};
