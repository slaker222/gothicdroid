#include "resources.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <tuple>
#include <functional>
#include <cstring>

#include <Tempest/MemReader>
#include <Tempest/Pixmap>
#include <Tempest/Device>
#include <Tempest/Dir>
#include <Tempest/Application>
#include <Tempest/Sound>
#include <Tempest/SoundEffect>
#include <Tempest/TextCodec>
#include <Tempest/Log>
#include <Tempest/Color>

#include <zenkit/MultiResolutionMesh.hh>
#include <zenkit/ModelHierarchy.hh>
#include <zenkit/Model.hh>
#include <zenkit/ModelScript.hh>
#include <zenkit/Material.hh>
#include <zenkit/Texture.hh>
#include <zenkit/addon/texcvt.hh>
#include <zenkit/Texture.hh>

#include "graphics/mesh/submesh/pfxemittermesh.h"
#include "graphics/mesh/submesh/packedmesh.h"
#include "graphics/mesh/skeleton.h"
#include "graphics/mesh/protomesh.h"
#include "graphics/mesh/animation.h"
#include "graphics/mesh/attachbinder.h"
#include "graphics/material.h"
#include "dmusic/directmusic.h"
#include "sound/opussound.h"
#include "utils/fileext.h"
#include "utils/gthfont.h"

#include "gothic.h"
#include "utils/string_frm.h"

#include <dmusic.h>

using namespace Tempest;

Resources* Resources::inst=nullptr;

static void emplaceTag(char* buf, char tag){
  for(size_t i=1;buf[i];++i){
    if(buf[i]==tag && buf[i-1]=='_' && buf[i+1]=='0'){
      buf[i  ]='%';
      buf[i+1]='s';
      ++i;
      return;
      }
    }
  }

Resources::Resources(Tempest::Device &device)
  : dev(device) {
  inst=this;

  static const uint16_t index[] = {
      0, 1, 2, 0, 2, 3,
      4, 6, 5, 4, 7, 6,
      1, 5, 2, 2, 5, 6,
      4, 0, 7, 7, 0, 3,
      3, 2, 7, 7, 2, 6,
      4, 5, 0, 0, 5, 1
    };
  cube = device.ibo(index, sizeof(index)/sizeof(index[0]));

  dxMusic.reset(new Dx8::DirectMusic());
  // G2
  dxMusic->addPath(Gothic::nestedPath({u"_work",u"Data",u"Music",u"newworld"},  Dir::FT_Dir));
  dxMusic->addPath(Gothic::nestedPath({u"_work",u"Data",u"Music",u"AddonWorld"},Dir::FT_Dir));
  // G1
  dxMusic->addPath(Gothic::nestedPath({u"_work",u"Data",u"Music",u"dungeon"},  Dir::FT_Dir));
  dxMusic->addPath(Gothic::nestedPath({u"_work",u"Data",u"Music",u"menu_men"}, Dir::FT_Dir));
  dxMusic->addPath(Gothic::nestedPath({u"_work",u"Data",u"Music",u"orchestra"},Dir::FT_Dir));
  // switch-build
  dxMusic->addPath(Gothic::nestedPath({u"_work",u"Data",u"Music"},Dir::FT_Dir));

  fBuff .reserve(8*1024*1024);
  ddsBuf.reserve(8*1024*1024);

  {
  Pixmap pm(1,1,TextureFormat::RGBA8);
  uint8_t* pix = reinterpret_cast<uint8_t*>(pm.data());
  pix[0]=255;
  pix[3]=255;
  fallback = device.texture(pm);
  }

  {
  Pixmap pm(1,1,TextureFormat::RGBA8);
  fbZero = device.texture(pm);
  }

  fbImg = device.image2d(TextureFormat::R32U,1,1);

  // Set up the DirectMusic loader
  DmResult rv = DmLoader_create(&dmLoader, DmLoader_DOWNLOAD);
  if(rv != DmResult_SUCCESS) {
    Log::e("Failed to created DmLoader object. Out of memory?");
    }

  DmLoader_addResolver(dmLoader, [](void* ctx, char const* name, size_t* len) -> void* {
      auto* slf = reinterpret_cast<Resources*>(ctx);
      auto* node = slf->gothicAssets.find(name);

      if (node == nullptr) {
        return nullptr;
      }

      auto reader = node->open_read();

      reader->seek(0, zenkit::Whence::END);
      *len = reader->tell();

      reader->seek(0, zenkit::Whence::BEG);
      auto* bytes = static_cast<uint8_t *>(malloc(*len));
      reader->read(bytes, *len);

      return bytes;
  }, this);
  }

void Resources::mountWork(const std::filesystem::path& path) {
  inst->gothicAssets.mkdir("/_work");
  inst->gothicAssets.mount_host(path, "/_work", zenkit::VfsOverwriteBehavior::NONE);
  }

bool Resources::mountOver(const std::filesystem::path& path) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  try {
    inst->gothicAssets.mount_disk(path, zenkit::VfsOverwriteBehavior::ALL);
    return true;
    }
  catch(const std::exception& err) {
    Log::e("unable to mount archive: \"", path.string(), "\", reason: ", err.what());
    return false;
    }
  }

static std::string lowerCase(std::string_view s) {
  std::string r(s);
  for(auto& c:r)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return r;
  }

bool Resources::mountTextHost(const std::filesystem::path& dir) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  try {
    const auto work = dir/"_work";
    if(!std::filesystem::exists(work))
      return true;
    inst->gothicAssets.mount_host(work, "/_work", zenkit::VfsOverwriteBehavior::ALL);
    if(const auto* dat = inst->gothicAssets.find("GOTHIC.DAT")) {
      auto rd = dat->open_read();
      rd->seek(0, zenkit::Whence::END);
      Log::i("text files mounted from \"", dir.string(), "\": GOTHIC.DAT is ", rd->tell(), " bytes");
      }
    return true;
    }
  catch(const std::exception& err) {
    Log::e("unable to mount text files: \"", dir.string(), "\", reason: ", err.what());
    return false;
    }
  }

static void collectPaths(const zenkit::VfsNode& root, std::vector<std::pair<std::string,std::string>>& out) {
  // (lower-case path, name) of every file under root
  std::vector<std::pair<const zenkit::VfsNode*,std::string>> tree{{&root,std::string()}};
  while(!tree.empty()) {
    auto [n,prefix] = tree.back();
    tree.pop_back();
    for(auto& c:n->children()) {
      if(c.type()==zenkit::VfsNodeType::DIRECTORY)
        tree.emplace_back(&c,prefix+lowerCase(c.name())+"/"); else
        out.emplace_back(prefix+lowerCase(c.name()),c.name());
      }
    }
  }

static std::string lowerBaseName(const std::filesystem::path& p) {
  return lowerCase(p.filename().string());
  }

void Resources::scanTextArchive(const std::filesystem::path& path) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  try {
    zenkit::Vfs tmp;
    tmp.mount_disk(path, zenkit::VfsOverwriteBehavior::ALL);
    std::vector<std::pair<std::string,std::string>> files;
    collectPaths(tmp.root(),files);
    for(auto& f:files)
      inst->textActive.insert(f.first);
    inst->textArchiveNames.insert(lowerBaseName(path));
    }
  catch(const std::exception& err) {
    Log::e("unable to scan archive: \"", path.string(), "\", reason: ", err.what());
    }
  }

bool Resources::mountTextArchive(const std::filesystem::path& path) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  try {
    // what the archive holds is what the caches may have to forget
    zenkit::Vfs tmp;
    tmp.mount_disk(path, zenkit::VfsOverwriteBehavior::ALL);
    std::vector<std::pair<std::string,std::string>> files;
    collectPaths(tmp.root(),files);
    for(auto& f:files) {
      inst->textNew.insert(f.first);
      inst->textOverride.insert(lowerCase(f.second));
      }
    inst->textArchiveNames.insert(lowerBaseName(path));
    inst->gothicAssets.mount_disk(path, zenkit::VfsOverwriteBehavior::ALL);
    return true;
    }
  catch(const std::exception& err) {
    Log::e("unable to mount archive: \"", path.string(), "\", reason: ", err.what());
    return false;
    }
  }

void Resources::restoreOriginals(const std::set<std::string>& paths) {
  struct Src {
    std::filesystem::path archive;
    int64_t               time = -1;
    uint32_t              off  = 0, size = 0;
    std::string           dir, name;
    };
  std::map<std::string,Src> best;

  std::vector<Archive> archives;
  detectVdf(archives,Gothic::inst().nestedPath({u"Data"},Dir::FT_Dir));
  for(auto& ar:archives) {
    if(ar.isMod)
      continue;
    const std::filesystem::path file = TextCodec::toUtf8(ar.name);
    if(textArchiveNames.count(lowerBaseName(file))>0)
      continue;
    try {
      std::ifstream in(file,std::ios::binary);
      char hdr[296] = {};
      in.read(hdr,sizeof(hdr));
      if(!in)
        continue;
      uint32_t count=0, root=0;
      std::memcpy(&count,hdr+272,4);
      std::memcpy(&root, hdr+288,4);
      if(count==0 || count>1000000)
        continue;
      struct Entry { char name[64]; uint32_t off, size, type, attr; };
      std::vector<Entry> table(count);
      in.seekg(root);
      in.read(reinterpret_cast<char*>(table.data()),std::streamsize(count*sizeof(Entry)));
      if(!in)
        continue;
      auto trim = [](const char* n){
        std::string s(n,strnlen(n,64));
        while(!s.empty() && (s.back()==' ' || s.back()=='\0'))
          s.pop_back();
        return s;
        };
      std::function<void(uint32_t,const std::string&,int)> walk = [&](uint32_t at, const std::string& dir, int depth) {
        for(; at<count && depth<32; ++at) {
          auto& e = table[at];
          const std::string name = trim(e.name);
          if(e.type & 0x80000000u) {
            walk(e.off,dir+name+"/",depth+1);
            } else {
            const std::string key = lowerCase(dir+name);
            if(paths.count(key)>0) {
              auto& b = best[key];
              if(ar.time>b.time) {
                b.archive = file;
                b.time    = ar.time;
                b.off     = e.off;
                b.size    = e.size;
                b.dir     = dir;
                b.name    = name;
                }
              }
            }
          if(e.type & 0x40000000u)
            break;
          }
        };
      walk(0,"",0);
      }
    catch(const std::exception& err) {
      Log::e("unable to read archive: \"", file.string(), "\", reason: ", err.what());
      }
    }

  size_t n = 0;
  for(auto& i:best) {
    auto& s = i.second;
    try {
      auto buf = std::make_unique<std::byte[]>(s.size);
      std::ifstream in(s.archive,std::ios::binary);
      in.seekg(s.off);
      in.read(reinterpret_cast<char*>(buf.get()),s.size);
      if(!in)
        continue;
      std::string parent = "/"+s.dir;
      if(parent.size()>1)
        parent.pop_back();
      gothicAssets.mount(zenkit::VfsNode::file(s.name,zenkit::VfsFileDescriptor{buf.get(),s.size,false},std::time_t(s.time)),
                         parent,zenkit::VfsOverwriteBehavior::ALL);
      restoredData.push_back(std::move(buf));
      textOverride.insert(lowerCase(s.name));
      ++n;
      }
    catch(const std::exception& err) {
      Log::e("unable to restore \"", i.first, "\": ", err.what());
      }
    }
  Log::i("text language: ",n," of ",paths.size()," replaced files put back from the shared archives");
  }

void Resources::implFinishTextMount() {
  // what the previous text language's archives held and the new one does not is put back
  std::set<std::string> gone;
  for(auto& p:textActive)
    if(textNew.count(p)==0)
      gone.insert(p);
  restoreOriginals(gone);
  textActive = std::move(textNew);
  textNew.clear();
  }

void Resources::reloadTextAssets() {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  std::lock_guard<std::recursive_mutex> gf(inst->syncFont);
  inst->implFinishTextMount();
  auto replaced = [&](const std::string& name) {
    auto n = lowerCase(name);
    if(inst->textOverride.count(n)>0)
      return true;
    if(n.size()>4 && n.compare(n.size()-4,4,".tga")==0)
      return inst->textOverride.count(n.substr(0,n.size()-4)+"-c.tex")>0;
    return false;
    };

  size_t nTex = 0, nFont = 0;
  for(auto& i:inst->texCache) {
    if(i.second==nullptr || !replaced(i.first))
      continue;
    auto fresh = inst->implLoadTextureUncached(i.first,false);
    if(!fresh.isEmpty()) {
      *i.second = std::move(fresh);
      ++nTex;
      }
    }

  for(auto& i:inst->gothicFnt) {
    auto&       key   = i.first;
    std::string_view name = std::get<0>(key);
    const FontType   type = std::get<1>(key);
    const float      sc   = std::get<2>(key);
    std::string_view file = name.substr(0,name.find('.'));
    string_frm tex, fnt;
    if(type==FontType::Hi) {
      tex = string_frm(file,"_hi.tga");
      fnt = string_frm(file,"_hi.fnt");
      } else {
      tex = string_frm(file,".tga");
      fnt = string_frm(file,".fnt");
      }
    Tempest::Color color(1.f);
    if(type==FontType::Disabled)
      color = Tempest::Color(1.f,1.f,1.f,0.6f);
    else if(type==FontType::Yellow)
      color = Tempest::Color(1.f,1.f,0.1f,1.f);
    else if(type==FontType::Red)
      color = Tempest::Color(1.f,0.f,0.f,1.f);
    const auto* entry = Resources::vdfsIndex().find(fnt);
    if(entry==nullptr)
      continue;
    GthFont f(*entry->open_read(),tex,color);
    f.setScale(sc);
    *i.second = std::move(f);
    ++nFont;
    }
  inst->textOverride.clear();
  Log::i("text language: ",nTex," textures and ",nFont," fonts loaded again");
  }

void Resources::loadVdfs(const std::vector<std::u16string>& modvdfs, bool modFilter) {
  std::vector<Archive> archives;
  inst->detectVdf(archives,Gothic::inst().nestedPath({u"Data"},Dir::FT_Dir));

  // Remove all mod files, that are not listed in modvdfs
  if(modFilter) {
    // NOTE: apparently in CoM there is no mods list declaration. In such case - assume all modes
    archives.erase(std::remove_if(archives.begin(), archives.end(),
                  [&modvdfs](const Archive& a){
                    return a.isMod && modvdfs.end() == std::find_if(modvdfs.begin(), modvdfs.end(),
                          [&a](const std::u16string& modname) {
                            const std::u16string_view& full_path = a.name;
                            const std::u16string_view& file_name = modname;
                            return (0 == full_path.compare(full_path.length() - file_name.length(),
                                                           file_name.length(), file_name));
                            });
                    }), archives.end());
    }

  // addon archives first!
  std::stable_sort(archives.begin(),archives.end(),[](const Archive& a,const Archive& b){
    int aIsMod = a.isMod ? 1 : -1;
    int bIsMod = b.isMod ? 1 : -1;
    return std::make_tuple(aIsMod,a.time,int(a.ord)) >=
           std::make_tuple(bIsMod,b.time,int(b.ord));
    });

  for(auto& i:archives) {
    try {
#if !defined(__EMSCRIPTEN__)
      auto in = zenkit::Read::from(i.name);
#endif
#ifdef __IOS__
      // causes OOM on iPhone7
      if(i.name.find(u"Speech")!=std::string::npos)
        continue;
#endif
      inst->gothicAssets.mount_disk(i.name, zenkit::VfsOverwriteBehavior::OLDER);
      }
    catch(const zenkit::VfsBrokenDiskError& err) {
      Log::e("unable to load archive: \"", TextCodec::toUtf8(i.name), "\", reason: ", err.what());
      }
    catch(const std::exception& err) {
      Log::e("unable to load archive: \"", TextCodec::toUtf8(i.name), "\", reason: ", err.what());
      }
    }

  //for(auto& i:inst->gothicAssets.getKnownFiles())
  //  Log::i(i);

  // auto v = getFileData("DRAGONISLAND.ZEN");
  // Tempest::WFile f("../../internal/DRAGONISLAND.ZEN");
  // f.write(v.data(),v.size());
  }

Resources::~Resources() {
  DmLoader_release(dmLoader);
  inst=nullptr;
  }

bool Resources::hasFile(std::string_view name) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->gothicAssets.find(name) != nullptr;
  }

bool Resources::getFileData(std::string_view name, std::vector<uint8_t> &dat) {
  dat.clear();

  const auto* entry = Resources::vdfsIndex().find(name);
  if(entry==nullptr)
    return false;

  // TODO: This should return a buffer!
  auto reader = entry->open_read();

  reader->seek(0, zenkit::Whence::END);
  dat.resize(reader->tell());
  reader->seek(0, zenkit::Whence::BEG);
  reader->read(dat.data(), dat.size());

  return true;
  }

std::vector<uint8_t> Resources::getFileData(std::string_view name) {
  std::vector<uint8_t> data;
  getFileData(name,data);
  return data;
  }

std::unique_ptr<zenkit::Read> Resources::getFileBuffer(std::string_view name) {
  const auto* entry = Resources::vdfsIndex().find(name);
  if (entry == nullptr)
    throw std::runtime_error("failed to open resource: " + std::string{name});
  return entry->open_read();
  }

std::unique_ptr<zenkit::ReadArchive> Resources::openReader(std::string_view name, std::unique_ptr<zenkit::Read>& read) {
  const auto* entry = Resources::vdfsIndex().find(name);
  if(entry == nullptr)
    throw std::runtime_error("failed to open resource: " + std::string{name});
  auto buf = entry->open_read();
  if(buf == nullptr)
    throw std::runtime_error("failed to open resource: " + std::string{name});
  auto zen = zenkit::ReadArchive::from(buf.get());
  if(zen == nullptr)
    throw std::runtime_error("failed to open resource: " + std::string{name});
  read = std::move(buf);
  return zen;
  }

const char* Resources::renderer() {
  return inst->dev.properties().name;
  }

static Sampler implShadowSampler() {
  Tempest::Sampler smp;
  smp.setFiltration(Tempest::Filter::Nearest);
  smp.setClamping(Tempest::ClampMode::ClampToEdge);
  smp.anisotropic = false;
  return smp;
  }

const Sampler& Resources::shadowSampler() {
  static Tempest::Sampler smp = implShadowSampler();
  return smp;
  }

void Resources::detectVdf(std::vector<Archive>& ret, const std::u16string &root) {
  Dir::scan(root,[this,&root,&ret](const std::u16string& vdf, Dir::FileType t){
    if(t==Dir::FT_File) {
      auto file = root + vdf;
      Archive ar;
      ar.name  = root+vdf;
      ar.time  = vdfTimestamp(ar.name);
      ar.ord   = uint16_t(ret.size());
      ar.isMod = vdf.rfind(u".mod")==vdf.size()-4;

      if(std::filesystem::file_size(ar.name)>0)
        ret.emplace_back(std::move(ar));
      return;
      }

    // switch uses NX extension for language dependant stuff
    if(vdf.find(u".NX.")!=std::string::npos)
      return;

    if(t==Dir::FT_Dir && vdf!=u".." && vdf!=u".") {
      auto dir = root + vdf + u"/";
      detectVdf(ret,dir);
      }
    });
  }

const GthFont& Resources::dialogFont(const float scale) {
  return font("font_old_10_white.tga",FontType::Normal,scale);
  }

const GthFont& Resources::font(const float scale) {
  return font("font_old_10_white.tga",FontType::Normal,scale);
  }

const GthFont& Resources::font(Resources::FontType type, const float scale) {
  return font("font_old_10_white.tga",type,scale);
  }

const GthFont& Resources::font(std::string_view fname, FontType type, const float scale) {
  if(fname.empty())
    return font(scale);
  return inst->implLoadFont(fname, type, scale);
  }

const Texture2d& Resources::fallbackTexture() {
  return inst->fallback;
  }

const Texture2d &Resources::fallbackBlack() {
  return inst->fbZero;
  }

const Tempest::StorageImage& Resources::fallbackImage() {
  return inst->fbImg;
  }

const zenkit::Vfs& Resources::vdfsIndex() {
  return inst->gothicAssets;
  }

const Tempest::IndexBuffer<uint16_t>& Resources::cubeIbo() {
  return inst->cube;
  }

int64_t Resources::vdfTimestamp(const std::u16string& name) {
  enum {
    VDF_COMMENT_LENGTH   = 256,
    VDF_SIGNATURE_LENGTH = 16,
    };
  uint32_t count=0, timestamp=0;
  char sign[VDF_SIGNATURE_LENGTH]={};

  try {
    RFile fin(name);
    fin.seek(VDF_COMMENT_LENGTH);
    fin.read(sign,VDF_SIGNATURE_LENGTH);
    fin.read(&count,sizeof(count));
    fin.seek(4);
    fin.read(&timestamp,sizeof(timestamp));

    return int64_t(timestamp);
    }
  catch(...) {
    return -1;
    }
  }

Tempest::Texture2d* Resources::implLoadTexture(std::string_view cname, bool forceMips, const zenkit::Vfs* vfs) {
  if(cname.empty())
    return nullptr;

  //TODO: __cpp_lib_generic_unordered_lookup
  std::string name = std::string(cname);
  if(vfs!=nullptr)
    name = "\x01sky" + std::to_string(vfs==&skyAssets[0] ? 1 : 2) + "/" + name; // the games have textures of the same names
  auto it=texCache.find(name);
  if(it!=texCache.end())
    return it->second.get();

  lastDxt1Decoded = false;
  auto tex = implLoadTextureUncached(cname, forceMips, vfs);
  const bool dxt1Dec = lastDxt1Decoded;
  if(!tex.isEmpty()) {
    std::unique_ptr<Texture2d> t{new Texture2d(std::move(tex))};
    Texture2d* ret=t.get();
    if(dxt1Dec)
      dxt1Decoded.insert(ret);
    texCache[std::move(name)] = std::move(t);
    return ret;
    }
  texCache[std::move(name)] = nullptr;
  return nullptr;
  }

Texture2d Resources::implLoadTextureUncached(std::string_view name, bool forceMips, const zenkit::Vfs* vfsPtr) {
  if(name.empty())
    return Texture2d();
  const zenkit::Vfs& vfs = vfsPtr!=nullptr ? *vfsPtr : Resources::vdfsIndex();

  if(FileExt::hasExt(name,"TGA")) {
    auto nameAlt = std::string(name);
    nameAlt.resize(nameAlt.size() + 2);
    std::memcpy(&nameAlt[0]+nameAlt.size()-6, "-C.TEX", 6);

    if(const auto* entry = vfs.find(nameAlt)) {
      zenkit::Texture tex;

      auto reader = entry->open_read();
      tex.load(reader.get());

      const bool isDxt = (tex.format() == zenkit::TextureFormat::DXT1 ||
                          tex.format() == zenkit::TextureFormat::DXT2 ||
                          tex.format() == zenkit::TextureFormat::DXT3 ||
                          tex.format() == zenkit::TextureFormat::DXT4 ||
                          tex.format() == zenkit::TextureFormat::DXT5);
      const bool canDxt = dev.properties().hasSamplerFormat(TextureFormat::DXT1);

      if(isDxt && canDxt) {
        auto dds = zenkit::to_dds(tex);
        auto ddsRead = zenkit::Read::from(dds);

        return implLoadTextureUncached(name, *ddsRead, forceMips);
        } else {
        auto rgba = tex.as_rgba8(0);
        lastDxt1Decoded = (tex.format() == zenkit::TextureFormat::DXT1);

        try {
          Tempest::Pixmap    pm(tex.width(), tex.height(), TextureFormat::RGBA8);
          std::memcpy(pm.data(), rgba.data(), rgba.size());
          const bool useMipmap = forceMips || (tex.mipmaps() > 1);
          return dev.texture(pm, useMipmap);
          }
        catch (...) {
          }
        }
      }
    }

  if(auto* entry = vfs.find(name)) {
    auto reader = entry->open_read();
    return implLoadTextureUncached(name, *reader, forceMips);
    }
  return Texture2d();
  }

Texture2d Resources::implLoadTextureUncached(std::string_view name, zenkit::Read& data, bool forceMips) {
  try {
    std::vector<uint8_t> raw;
    data.seek(0, zenkit::Whence::END);
    raw.resize(data.tell());
    data.seek(0, zenkit::Whence::BEG);
    data.read(raw.data(), raw.size());

    Tempest::MemReader rd((uint8_t*)raw.data(), raw.size());
    Tempest::Pixmap    pm(rd);

    const bool useMipmap = forceMips || (pm.mipCount()>1); // do not generate mips, if original texture has has none
    return dev.texture(pm, useMipmap);
    }
  catch(...){
    return Texture2d();
    }
  }

ProtoMesh* Resources::implLoadMesh(std::string_view name) {
  if(name.size()==0)
    return nullptr;

  auto cname = std::string(name);
  auto it    = aniMeshCache.find(cname);
  if(it!=aniMeshCache.end())
    return it->second.get();

  auto  t   = implLoadMeshMain(cname);
  auto  ret = t.get();
  aniMeshCache[cname] = std::move(t);
  if(ret==nullptr)
    Log::e("unable to load mesh \"",cname,"\"");
  return ret;
  }

std::unique_ptr<ProtoMesh> Resources::implLoadMeshMain(std::string name) {
  if(FileExt::hasExt(name,"3DS")) {
    FileExt::exchangeExt(name,"3DS","MRM");

    const auto* entry = Resources::vdfsIndex().find(name);
    if(entry == nullptr)
      return nullptr;

    zenkit::MultiResolutionMesh zmsh;

    auto reader = entry->open_read();
    zmsh.load(reader.get());

    if(zmsh.sub_meshes.empty())
      return nullptr;

    PackedMesh packed(zmsh,PackedMesh::PK_Visual);
    return std::unique_ptr<ProtoMesh>{new ProtoMesh(std::move(packed),name)};
    }

  if(FileExt::hasExt(name,"MMS") || FileExt::hasExt(name,"MMB")) {
    FileExt::exchangeExt(name,"MMS","MMB");

    const auto* entry = Resources::vdfsIndex().find(name);
    if(entry == nullptr)
      throw std::runtime_error("failed to open resource: " + name);

    zenkit::MorphMesh zmm;
    auto reader = entry->open_read();
    zmm.load(reader.get());

    if(zmm.mesh.sub_meshes.empty())
      return nullptr;

    PackedMesh packed(zmm.mesh,PackedMesh::PK_VisualMorph);
    return std::unique_ptr<ProtoMesh>{new ProtoMesh(std::move(packed),zmm.animations,name)};
    }

  if(FileExt::hasExt(name,"MDS")) {
    auto anim = Resources::loadAnimation(name);
    if(anim==nullptr)
      return nullptr;

    auto mesh   = std::string(anim->defaultMesh());

    FileExt::exchangeExt(mesh,nullptr,"MDM") ||
    FileExt::exchangeExt(mesh,"ASC",  "MDM");

    auto mdhName = std::string(anim->defaultMesh());
    if(mdhName.empty())
      mdhName = name;
    FileExt::assignExt(mdhName,"MDH");

    if(const auto* entryMdh = Resources::vdfsIndex().find(mdhName)) {
      // Find a MDH hirarchy file and separate mesh
      zenkit::ModelHierarchy mdh;
      auto reader = entryMdh->open_read();
      mdh.load(reader.get());

      std::unique_ptr<Skeleton> sk{new Skeleton(mdh,anim,name)};
      std::unique_ptr<ProtoMesh> t;

      if(const auto* entry = Resources::vdfsIndex().find(mesh)) {
        auto reader = entry->open_read();

        zenkit::ModelMesh mdm {};
        mdm.load(reader.get());
        t.reset(new ProtoMesh(mdm,std::move(sk),name));
        }
      else
        t.reset(new ProtoMesh(mdh,std::move(sk),name));

      return t;
      }

    // MDL files contain both the hirarchy and mesh, try to find one as a fallback
    FileExt::exchangeExt(mdhName,"MDH","MDL");
    if(const auto* entryMdl = Resources::vdfsIndex().find(mdhName)) {
      if(entryMdl==nullptr)
        throw std::runtime_error("failed to open resource: " + mdhName);

      zenkit::Model mdm;
      auto reader = entryMdl->open_read();
      mdm.load(reader.get());

      std::unique_ptr<Skeleton> sk{new Skeleton(mdm.hierarchy,anim,name)};
      std::unique_ptr<ProtoMesh> t{new ProtoMesh(mdm,std::move(sk),name)};
      return t;
      }
    }

  if(FileExt::hasExt(name,"MDM") || FileExt::hasExt(name,"ASC")) {
    FileExt::exchangeExt(name,"ASC","MDM");

    if(!hasFile(name))
      return nullptr;

    const auto* entry = Resources::vdfsIndex().find(name);
    if(entry == nullptr)
      return nullptr;

    zenkit::ModelMesh mdm;
    auto reader = entry->open_read();
    mdm.load(reader.get());

    std::unique_ptr<ProtoMesh> t{new ProtoMesh(std::move(mdm),nullptr,name)};
    return t;
    }

  if(FileExt::hasExt(name,"MDL")) {
    if(!hasFile(name))
      return nullptr;

    const auto* entry = Resources::vdfsIndex().find(name);
    if(entry == nullptr)
      throw std::runtime_error("failed to open resource: " + name);
    zenkit::Model mdm;
    auto reader = entry->open_read();
    mdm.load(reader.get());

    std::unique_ptr<Skeleton> sk{new Skeleton(mdm.hierarchy,nullptr,name)};
    std::unique_ptr<ProtoMesh> t{new ProtoMesh(mdm,std::move(sk),name)};
    return t;
    }

  if(FileExt::hasExt(name,"TGA")) {
    Log::e("decals should be loaded by Resources::implDecalMesh instead");
    }

  return nullptr;
  }

PfxEmitterMesh* Resources::implLoadEmiterMesh(std::string_view name) {
  // TODO: reuse code from Resources::implLoadMeshMain
  auto cname = std::string(name);
  auto it    = emiMeshCache.find(cname);
  if(it!=emiMeshCache.end())
    return it->second.get();

  auto& ret = emiMeshCache[cname];

  if(FileExt::hasExt(cname,"3DS")) {
    FileExt::exchangeExt(cname,"3DS","MRM");

    const auto* entry = Resources::vdfsIndex().find(cname);
    if (entry == nullptr) return nullptr;
    zenkit::MultiResolutionMesh zmsh;
    auto reader = entry->open_read();
    zmsh.load(reader.get());

    if(zmsh.sub_meshes.empty())
      return nullptr;

    PackedMesh packed(zmsh,PackedMesh::PK_Visual);
    ret = std::unique_ptr<PfxEmitterMesh>(new PfxEmitterMesh(packed));
    return ret.get();
    }

  if(FileExt::hasExt(name,"MDM")) {
    if(!hasFile(name))
      return nullptr;

    const auto* entry = Resources::vdfsIndex().find(cname);
    if(entry == nullptr)
      throw std::runtime_error("failed to open resource: " + cname);
    zenkit::ModelMesh mdm;
    auto reader = entry->open_read();
    mdm.load(reader.get());

    ret = std::unique_ptr<PfxEmitterMesh>(new PfxEmitterMesh(std::move(mdm)));
    return ret.get();
    }

  return nullptr;
  }

ProtoMesh* Resources::implDecalMesh(const zenkit::VisualDecal& decal) {
  DecalK key;
  key.mat         = Material(decal);
  key.sX          = decal.dimension.x;
  key.sY          = decal.dimension.y;
  key.decal2Sided = decal.two_sided;

  if(key.mat.tex==nullptr)
    return nullptr;

  auto it = decalMeshCache.find(key);
  if(it!=decalMeshCache.end())
    return it->second.get();

  Resources::Vertex vbo[8] = {
    {{-1.f, -1.f, 0.f},{0,0,-1},{0,1}, 0xFFFFFFFF},
    {{ 1.f, -1.f, 0.f},{0,0,-1},{1,1}, 0xFFFFFFFF},
    {{ 1.f,  1.f, 0.f},{0,0,-1},{1,0}, 0xFFFFFFFF},
    {{-1.f,  1.f, 0.f},{0,0,-1},{0,0}, 0xFFFFFFFF},

    {{-1.f, -1.f, 0.f},{0,0, 1},{0,1}, 0xFFFFFFFF},
    {{ 1.f, -1.f, 0.f},{0,0, 1},{1,1}, 0xFFFFFFFF},
    {{ 1.f,  1.f, 0.f},{0,0, 1},{1,0}, 0xFFFFFFFF},
    {{-1.f,  1.f, 0.f},{0,0, 1},{0,0}, 0xFFFFFFFF},
    };
  for(auto& i:vbo) {
    i.pos[0]*=key.sX;
    i.pos[1]*=key.sY;
    }

  std::vector<Resources::Vertex> cvbo(vbo,vbo+8);
  std::vector<uint32_t>          cibo;
  if(key.decal2Sided)
    cibo = { 0,1,2, 0,2,3, 4,6,5, 4,7,6 }; else
    cibo = { 0,1,2, 0,2,3 };

  std::unique_ptr<ProtoMesh> t{new ProtoMesh(key.mat, std::move(cvbo), std::move(cibo))};

  auto ret = t.get();
  decalMeshCache[key] = std::move(t);
  return ret;
  }

std::unique_ptr<Animation> Resources::implLoadAnimation(std::string name) {
  if(name.size()<4)
    return nullptr;

  if(Gothic::inst().version().game==2)
    FileExt::exchangeExt(name,"MDS","MSB");

  const auto* entry = Resources::vdfsIndex().find(name);
  if(entry == nullptr)
    return nullptr;

  if(FileExt::hasExt(name,"MSB") || FileExt::hasExt(name,"MDS")) {
    zenkit::ModelScript mds;
    auto reader = entry->open_read();
    try {
      mds.load(reader.get());
      }
    catch(const std::exception& err) {
      // a broken animation script of a mod (a typo, an odd comment) must not abort the loading of the whole world:
      // the original engine reads such files, the model just has no animations from it here
      Log::e("unable to parse animation script \"", name, "\": ", err.what());
      return nullptr;
      }
    return std::unique_ptr<Animation>{new Animation(mds,name.substr(0,name.size()-4),false)};
    }

  return nullptr;
  }

Dx8::PatternList Resources::implLoadDxMusic(std::string_view name) {
  auto u = Tempest::TextCodec::toUtf16(name);
  return dxMusic->load(u.c_str());
  }

DmSegment* Resources::implLoadMusicSegment(char const* name) {
  DmSegment* sgt = nullptr;
  DmResult rv = DmLoader_getSegment(dmLoader, name, &sgt);
  if(rv != DmResult_SUCCESS) {
    Log::e("Music segment not found: ", name);
    return nullptr;
    }
  return sgt;
  }

Tempest::Sound Resources::implLoadSoundBuffer(std::string_view name) {
  if(name.empty())
    return Tempest::Sound();

  if(!getFileData(name,fBuff))
    return Tempest::Sound();
  try {
    if(OpusSound::isOpus(fBuff.data(),fBuff.size())) {
      auto wav = OpusSound::decodeToWav(fBuff.data(),fBuff.size());
      if(wav.empty())
        throw std::runtime_error("bad opus stream");
      Tempest::MemReader rd(wav.data(),wav.size());
      return Tempest::Sound(rd);
      }
    Tempest::MemReader rd(fBuff.data(),fBuff.size());
    return Tempest::Sound(rd);
    }
  catch(...) {
    auto cname = std::string (name);
    Log::e("unable to load sound \"",cname,"\"");
    return Tempest::Sound();
    }
  }

GthFont &Resources::implLoadFont(std::string_view name, FontType type, const float scale) {
  std::lock_guard<std::recursive_mutex> g(inst->syncFont);

  auto key   = FontK(std::string(name), type, scale);
  auto it    = gothicFnt.find(key);
  if(it!=gothicFnt.end())
    return *(*it).second;

  std::string_view file = name;
  for(size_t i=0; i<name.size();++i) {
    if(name[i]=='.') {
      file = name.substr(0, i);
      break;
      }
    }

  string_frm tex, fnt;
  switch(type) {
    case FontType::Normal:
    case FontType::Disabled:
    case FontType::Yellow:
    case FontType::Red:
      tex = string_frm(file,".tga");
      fnt = string_frm(file,".fnt");
      break;
    case FontType::Hi:
      tex = string_frm(file,"_hi.tga");
      fnt = string_frm(file,"_hi.fnt");
      break;
    }

  auto color = Tempest::Color(1.f);
  switch(type) {
    case FontType::Normal:
    case FontType::Hi:
      color = Tempest::Color(1.f);
      break;
    case FontType::Disabled:
      color = Tempest::Color(1.f,1.f,1.f,0.6f);
      break;
    case FontType::Yellow:
      color = Tempest::Color(1.f,1.f,0.1f,1.f);
      //color = Tempest::Color(0.81f,0.78f,0.01f,1.f);
      break;
    case FontType::Red:
      color = Tempest::Color(1.f,0.f,0.f,1.f);
      break;
    }

  const auto* entry = Resources::vdfsIndex().find(fnt);
  if(entry == nullptr) {
    Log::e("failed to open resource: ", fnt);
    // throw std::runtime_error("failed to open resource: " + std::string{fnt});
    auto ptr   = std::make_unique<GthFont>();
    GthFont* f = ptr.get();
    gothicFnt[key] = std::move(ptr);
    return *f;
    }

  auto ptr   = std::make_unique<GthFont>(*entry->open_read(),tex,color);
  GthFont* f = ptr.get();
  f->setScale(scale);
  gothicFnt[key] = std::move(ptr);
  return *f;
  }

void Resources::mountSkyPack(int game) {
  const int i = game-1;
  if(i<0 || i>1 || skyOk[i])
    return;
  // the app downloads the pack when the player chooses the sky (it can come after the game started): asked again until it is there
  const char* env = std::getenv("OG_SKY_PACK_DIR");
  if(env==nullptr || *env=='\0')
    return;
  try {
    const std::filesystem::path dir = std::filesystem::path(env) / (game==1 ? "g1" : "g2");
    if(!std::filesystem::is_regular_file(dir / "SKYDAY_LAYER0_A0-C.TEX"))
      return;
    if(!skyTried[i]) {
      skyTried[i] = true;
      skyAssets[i].mkdir("/sky");
      }
    // the files that came later are seen by a new mount
    skyAssets[i].mount_host(dir, "/sky", zenkit::VfsOverwriteBehavior::ALL);
    skyOk[i] = skyAssets[i].find("SKYDAY_LAYER0_A0-C.TEX")!=nullptr;
    Log::i("sky: the sky pack of Gothic ",game,skyOk[i] ? " is mounted (" : " is empty (",dir.string(),")");
    }
  catch(const std::exception& err) {
    Log::e("sky: the sky pack of Gothic ",game," is not readable: ",err.what());
    }
  }

bool Resources::hasSkyPack(int game) {
  if(game<1 || game>2)
    return false;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->mountSkyPack(game);
  return inst->skyOk[game-1];
  }

const Texture2d* Resources::loadSkyTexture(int game, std::string_view name, bool forceMips) {
  if(game<1 || game>2)
    return nullptr;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->mountSkyPack(game);
  if(!inst->skyOk[game-1])
    return nullptr;
  // the key of the cache has the game in it: the two games have textures of the same names
  return inst->implLoadTexture(name,forceMips,&inst->skyAssets[game-1]);
  }

Texture2d Resources::loadTextureUncached(std::string_view name, bool forceMips) {
  return inst->implLoadTextureUncached(name, forceMips);
  }

const Texture2d* Resources::loadTexture(std::string_view name, bool forceMips) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implLoadTexture(name,forceMips);
  }

bool Resources::isDxt1Decoded(const Tempest::Texture2d* tex) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->dxt1Decoded.find(tex)!=inst->dxt1Decoded.end();
  }

const Texture2d* Resources::loadTexture(Tempest::Color color) {
  if(color==Color())
    return nullptr;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  auto& cache = inst->pixCache;
  auto it = cache.find(color);
  if(it!=cache.end())
    return it->second.get();

  uint8_t iv[4] = { uint8_t(255.f*color.r()), uint8_t(255.f*color.g()), uint8_t(255.f*color.b()), uint8_t(255.f*color.a()) };
  Pixmap p2(1,1,TextureFormat::RGBA8);
  std::memcpy(p2.data(),iv,4);

  auto t       = std::make_unique<Texture2d>(inst->dev.texture(p2));
  auto ret     = t.get();
  cache[color] = std::move(t);
  return ret;
  }

const Texture2d *Resources::loadTexture(std::string_view name, int32_t iv, int32_t ic) {
  if(name.size()>=128)
    return loadTexture(name);

  char v[16]={};
  char c[16]={};
  char buf1[128]={};
  char buf2[128]={};

  std::snprintf(v,sizeof(v),"V%d",iv);
  std::snprintf(c,sizeof(c),"C%d",ic);
  std::snprintf(buf1,sizeof(buf1),"%.*s",int(name.size()),name.data());

  emplaceTag(buf1,'V');
  std::snprintf(buf2,sizeof(buf2),buf1,v);

  emplaceTag(buf2,'C');
  std::snprintf(buf1,sizeof(buf1),buf2,c);

  return loadTexture(buf1);
  }

std::vector<const Texture2d*> Resources::loadTextureAnim(std::string_view name) {
  std::vector<const Texture2d*> ret;
  if(name.find("_A0")==std::string::npos &&
     name.find("_a0")==std::string::npos)
    return ret;

  for(int id=0; ; ++id) {
    size_t at = 0;
    char   buf[128]={};
    for(size_t i=0;i<name.size();++i) {
      if(i+2<name.size() && name[i]=='_' && (name[i+1]=='A' || name[i+1]=='a') && name[i+2]=='0'){
        at += size_t(std::snprintf(buf+at,sizeof(buf)-at,"_A%d",id));
        i+=2;
        } else {
        buf[at] = name[i];
        if('a'<=buf[at] && buf[at]<='z')
          buf[at] = char(buf[at]+'A'-'a');
        at++;
        }
      if(at>sizeof(buf))
        return ret;
      }
   auto t = loadTexture(buf);
    if(t==nullptr) {
      string_frm buf2(buf,".TGA");
      t = loadTexture(buf2);
      if(t==nullptr)
        return ret;
      }
    ret.push_back(t);
    }
  }

Texture2d Resources::loadTexturePm(const Pixmap &pm) {
  if(pm.isEmpty()) {
    Pixmap p2(1,1,TextureFormat::R8);
    std::memset(p2.data(),0,1);
    return inst->dev.texture(p2);
    }
  return inst->dev.texture(pm);
  }

Material Resources::loadMaterial(const zenkit::Material& src, bool enableAlphaTest) {
  return Material(src,enableAlphaTest);
  }

const ProtoMesh* Resources::loadMesh(std::string_view name) {
  if(name.size()==0)
    return nullptr;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implLoadMesh(name);
  }

const PfxEmitterMesh* Resources::loadEmiterMesh(std::string_view name) {
  if(name.empty())
    return nullptr;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implLoadEmiterMesh(name);
  }

const Skeleton* Resources::loadSkeleton(std::string_view name) {
  auto s = Resources::loadMesh(name);
  if(s==nullptr)
    return nullptr;
  return s->skeleton.get();
  }

const Animation* Resources::loadAnimation(std::string_view name) {
  auto cname = std::string(name);

  std::lock_guard<std::recursive_mutex> g(inst->sync);
  auto& cache = inst->animCache;
  auto it=cache.find(cname);
  if(it!=cache.end())
    return it->second.get();
  auto t       = inst->implLoadAnimation(cname);
  auto ret     = t.get();
  cache[cname] = std::move(t);
  return ret;
  }

Tempest::Sound Resources::loadSoundBuffer(std::string_view name) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implLoadSoundBuffer(name);
  }

Dx8::PatternList Resources::loadDxMusic(std::string_view name) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implLoadDxMusic(name);
  }

DmSegment* Resources::loadMusicSegment(char const* name) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implLoadMusicSegment(name);
  }

// The crown of a splash: a ring of water with jagged spikes, a unit size (radius 1, height 1); the texture is streaks of
// water and droplets, opaque at the bottom and thinning to the top. Scaled and animated by the player (MoveAlgo).
ProtoMesh* Resources::implSplashMesh() {
  if(splashProto!=nullptr)
    return splashProto.get();

  const int W = 512, H = 64;
  Pixmap pm(W,H,TextureFormat::RGBA8);
  auto* pix = reinterpret_cast<uint8_t*>(pm.data());
  auto hash = [](int x, int y) {
    uint32_t h = uint32_t(x)*374761393u + uint32_t(y)*668265263u;
    h = (h^(h>>13))*1274126177u;
    return float((h^(h>>16))&0xFFFF)/65535.f;
    };
  for(int y=0; y<H; ++y)
    for(int x=0; x<W; ++x) {
      const float v      = float(y)/float(H-1);        // 0 top .. 1 bottom
      const float streak = 0.3f + 0.7f*hash(x/3, 3);   // thin vertical streaks of water
      const float drops  = hash(x,y)>0.985f ? 1.f : 0.f; // droplets
      float a = std::min(std::max(v*1.25f-0.1f,0.f),1.f)*streak;
      a = std::max(a, drops*0.8f*(1.f-v*0.5f));
      auto* p = pix + size_t(y*W+x)*4;
      const float lt = 0.78f + 0.22f*streak;
      p[0] = uint8_t(210.f*lt); p[1] = uint8_t(225.f*lt); p[2] = uint8_t(230.f*lt);
      p[3] = uint8_t(std::min(a*0.75f,1.f)*255.f);
      }
  splashTex.reset(new Texture2d(dev.texture(pm)));

  Material mat;
  mat.tex   = splashTex.get();
  mat.alpha = Material::Transparent;

  const int N = 28;
  std::vector<Resources::Vertex> vbo;
  std::vector<uint32_t>          ibo;
  auto add = [&](float r, float y, float u, float v, float nx, float nz) {
    Resources::Vertex vx = {{r*nx, y, r*nz},{nx,0.3f,nz},{u,v},0xFFFFFFFF};
    vbo.push_back(vx);
    return uint32_t(vbo.size()-1);
    };
  for(int side=0; side<2; ++side) {
    for(int i=0; i<N; ++i) {
      const float a0 = float(i)*6.2831853f/float(N), a1 = float(i+1)*6.2831853f/float(N), am = (a0+a1)*0.5f;
      const float tip = (i%2==0) ? 1.0f : 0.62f;               // jagged crown
      const float u0 = float(i)/float(N), u1 = float(i+1)/float(N);
      auto b0 = add(0.8f, 0.f,  u0, 1.f,  std::cos(a0), std::sin(a0));
      auto b1 = add(0.8f, 0.f,  u1, 1.f,  std::cos(a1), std::sin(a1));
      auto m0 = add(0.95f,0.45f,u0, 0.5f, std::cos(a0), std::sin(a0));
      auto m1 = add(0.95f,0.45f,u1, 0.5f, std::cos(a1), std::sin(a1));
      auto t  = add(1.05f,tip,  (u0+u1)*0.5f, 0.f, std::cos(am), std::sin(am));
      uint32_t q[9] = {b0,b1,m1, b0,m1,m0, m0,m1,t};
      if(side==0) {
        for(int k=0; k<9; ++k) ibo.push_back(q[k]);
        } else {
        for(int k=0; k<9; k+=3) { ibo.push_back(q[k]); ibo.push_back(q[k+2]); ibo.push_back(q[k+1]); }
        }
      }
    }
  splashProto.reset(new ProtoMesh(mat, std::move(vbo), std::move(ibo)));
  return splashProto.get();
  }

const ProtoMesh* Resources::splashMesh() {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implSplashMesh();
  }

ProtoMesh* Resources::implBlobShadowMesh() {
  if(blobMesh!=nullptr)
    return blobMesh.get();

  // soft dark circle: the shadow under a character
  const int sz = 64;
  Pixmap pm(sz,sz,TextureFormat::RGBA8);
  auto* pix = reinterpret_cast<uint8_t*>(pm.data());
  for(int y=0; y<sz; ++y)
    for(int x=0; x<sz; ++x) {
      const float dx = (float(x)+0.5f)/float(sz)*2.f-1.f;
      const float dy = (float(y)+0.5f)/float(sz)*2.f-1.f;
      const float r  = std::sqrt(dx*dx+dy*dy);
      float k = std::min(std::max((1.f-r)/0.3f, 0.f), 1.f);
      k = k*k*(3.f-2.f*k);
      auto* p = pix + size_t(y*sz+x)*4;
      p[0] = p[1] = p[2] = 0;
      p[3] = uint8_t(k*0.56f*255.f);
      }
  blobTex.reset(new Texture2d(dev.texture(pm)));

  Material mat;
  mat.tex   = blobTex.get();
  mat.alpha = Material::Transparent;

  // lies in the XZ plane, both sides are drawn
  Resources::Vertex vbo[8] = {
    {{-1.f, 0.f, -1.f},{0, 1,0},{0,1}, 0xFFFFFFFF},
    {{ 1.f, 0.f, -1.f},{0, 1,0},{1,1}, 0xFFFFFFFF},
    {{ 1.f, 0.f,  1.f},{0, 1,0},{1,0}, 0xFFFFFFFF},
    {{-1.f, 0.f,  1.f},{0, 1,0},{0,0}, 0xFFFFFFFF},
    {{-1.f, 0.f, -1.f},{0,-1,0},{0,1}, 0xFFFFFFFF},
    {{ 1.f, 0.f, -1.f},{0,-1,0},{1,1}, 0xFFFFFFFF},
    {{ 1.f, 0.f,  1.f},{0,-1,0},{1,0}, 0xFFFFFFFF},
    {{-1.f, 0.f,  1.f},{0,-1,0},{0,0}, 0xFFFFFFFF},
    };
  std::vector<Resources::Vertex> cvbo(vbo,vbo+8);
  std::vector<uint32_t>          cibo = { 0,1,2, 0,2,3, 4,6,5, 4,7,6 };
  blobMesh.reset(new ProtoMesh(mat, std::move(cvbo), std::move(cibo)));
  return blobMesh.get();
  }

const ProtoMesh* Resources::blobShadowMesh() {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implBlobShadowMesh();
  }

const ProtoMesh* Resources::decalMesh(const zenkit::VisualDecal& decal) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implDecalMesh(decal);
  }

const Resources::VobTree* Resources::loadVobBundle(std::string_view name) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  return inst->implLoadVobBundle(name);
  }

void Resources::resetRecycled(uint8_t fId) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->recycledId = fId;
  inst->recycled[fId].ssbo.clear();
  inst->recycled[fId].img.clear();
  inst->recycled[fId].att.clear();
  inst->recycled[fId].zb.clear();
  inst->recycled[fId].arr.clear();
  inst->recycled[fId].rtas.clear();
  }

void Resources::recycle(Tempest::DescriptorArray &&arr) {
  if(arr.isEmpty())
    return;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->recycled[inst->recycledId].arr.emplace_back(std::move(arr));
  }

void Resources::recycle(Tempest::StorageBuffer&& ssbo) {
  if(ssbo.isEmpty())
    return;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->recycled[inst->recycledId].ssbo.emplace_back(std::move(ssbo));
  }

void Resources::recycle(Tempest::StorageImage&& img) {
  if(img.isEmpty())
    return;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->recycled[inst->recycledId].img.emplace_back(std::move(img));
  }

void Resources::recycle(Tempest::Attachment&& img) {
  if(img.isEmpty())
    return;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->recycled[inst->recycledId].att.emplace_back(std::move(img));
  }

void Resources::recycle(Tempest::ZBuffer&& img) {
  if(img.isEmpty())
    return;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->recycled[inst->recycledId].zb.emplace_back(std::move(img));
  }

void Resources::recycle(Tempest::AccelerationStructure&& rtas) {
  if(rtas.isEmpty())
    return;
  std::lock_guard<std::recursive_mutex> g(inst->sync);
  inst->recycled[inst->recycledId].rtas.emplace_back(std::move(rtas));
  }

const Resources::VobTree* Resources::implLoadVobBundle(std::string_view filename) {
  auto cname = std::string(filename);
  auto i     = zenCache.find(cname);
  if(i!=zenCache.end())
    return i->second.get();

  std::vector<std::shared_ptr<zenkit::VirtualObject>> bundle;
  try {
    const auto* entry = Resources::vdfsIndex().find(cname);
    if(entry == nullptr)
      throw std::runtime_error("failed to open resource: " + cname);

    zenkit::World wrld;

    auto reader = entry->open_read();
    wrld.load(reader.get(), Gothic::inst().version().game==1 ? zenkit::GameVersion::GOTHIC_1
                                                             : zenkit::GameVersion::GOTHIC_2);

    bundle = std::move(wrld.world_vobs);
    }
  catch(...) {
    Log::e("unable to load Zen-file: \"",cname,"\"");
    }

  auto ret = zenCache.insert(std::make_pair(filename,std::make_unique<VobTree>(std::move(bundle))));
  return ret.first->second.get();
  }

const AttachBinder *Resources::bindMesh(const ProtoMesh &anim, const Skeleton &s) {
  std::lock_guard<std::recursive_mutex> g(inst->sync);

  if(anim.submeshId.size()==0){
    static AttachBinder empty;
    return &empty;
    }
  BindK k = BindK(&s,&anim);

  auto it = inst->bindCache.find(k);
  if(it!=inst->bindCache.end())
    return it->second.get();

  std::unique_ptr<AttachBinder> ret(new AttachBinder(s,anim));
  auto p = ret.get();
  inst->bindCache[k] = std::move(ret);
  return p;
  }

Tempest::VertexBuffer<Resources::Vertex> Resources::sphere(int passCount, float R){
  std::vector<Resources::Vertex> r;
  r.reserve( size_t(4*pow(3, passCount+1)) );

  static const double pi = 3.141592654;

  Resources::Vertex v1 = {
    {1, 0, -0.5},
    {0,0,0},{0,0},0
    };
  Resources::Vertex v2 = {
    {float(cos(2*pi/3)), float(sin(2*pi/3)), -0.5},
    {0,0,0},{0,0},0
    };
  Resources::Vertex v3 = {
    {float(cos(2*pi/3)), -float(sin(2*pi/3)), -0.5},
    {0,0,0},{0,0},0
    };

  Resources::Vertex v4 = {
    {0, 0, 0.5},
    {0,0,0},{0,0},0
    };

  r.push_back(v1);
  r.push_back(v3);
  r.push_back(v2);

  r.push_back(v1);
  r.push_back(v2);
  r.push_back(v4);

  r.push_back(v2);
  r.push_back(v3);
  r.push_back(v4);

  r.push_back(v1);
  r.push_back(v4);
  r.push_back(v3);

  for(size_t i=0; i<r.size(); ++i){
    Resources::Vertex& v = r[i];
    float l = std::sqrt(v.pos[0]*v.pos[0] + v.pos[1]*v.pos[1] + v.pos[2]*v.pos[2]);

    v.pos[0] /= l;
    v.pos[1] /= l;
    v.pos[2] /= l;
    }

  for(int c=0; c<passCount; ++c){
    size_t maxI = r.size();
    for( size_t i=0; i<maxI; i+=3 ){
      Resources::Vertex x = {
        {
          0.5f*(r[i].pos[0]+r[i+1].pos[0]),
          0.5f*(r[i].pos[1]+r[i+1].pos[1]),
          0.5f*(r[i].pos[2]+r[i+1].pos[2])
        },
        {0,0,0},{0,0},0
      };
      Resources::Vertex y = {
        {
          0.5f*(r[i+2].pos[0]+r[i+1].pos[0]),
          0.5f*(r[i+2].pos[1]+r[i+1].pos[1]),
          0.5f*(r[i+2].pos[2]+r[i+1].pos[2])
        },
        {0,0,0},{0,0},0
      };
      Resources::Vertex z = {
        {
          0.5f*(r[i].pos[0]+r[i+2].pos[0]),
          0.5f*(r[i].pos[1]+r[i+2].pos[1]),
          0.5f*(r[i].pos[2]+r[i+2].pos[2])
        },
        {0,0,0},{0,0},0
      };

      r.push_back( r[i] );
      r.push_back( x );
      r.push_back( z );

      r.push_back( x );
      r.push_back( r[i+1] );
      r.push_back( y );

      r.push_back( y );
      r.push_back( r[i+2] );
      r.push_back( z );

      r[i]   = x;
      r[i+1] = y;
      r[i+2] = z;
      }

    for(size_t i=0; i<r.size(); ++i){
      Resources::Vertex & v = r[i];
      float l = std::sqrt(v.pos[0]*v.pos[0] + v.pos[1]*v.pos[1] + v.pos[2]*v.pos[2]);

      v.pos[0] /= l;
      v.pos[1] /= l;
      v.pos[2] /= l;
      }
    }

  for(size_t i=0; i<r.size(); ++i){
    Resources::Vertex & v = r[i];
    v.norm[0] = v.pos[0];
    v.norm[1] = v.pos[1];
    v.norm[2] = v.pos[2];

    v.pos[0] *= R;
    v.pos[1] *= R;
    v.pos[2] *= R;
    }

  return dev.vbo(r);
  }
