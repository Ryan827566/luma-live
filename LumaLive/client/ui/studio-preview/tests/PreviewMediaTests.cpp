#include "PreviewMedia.hpp"
#include <iostream>
#include <thread>
using namespace luma::client::ui::preview;
int main(){
    int failed=0;auto check=[&](bool value,const char* name){if(!value){std::cerr<<name<<'\n';++failed;}};
    VideoFrame f;f.width=2;f.height=2;f.format=PixelFormat::NV12;f.data={16,235,16,235,128,128};
    auto p=ToBgra(f);check(p && p->data[0]==0 && p->data[4]==255 && p->data[7]==255,"NV12 black/white");
    f.format=PixelFormat::I420;check(ToBgra(f)->data==p->data,"I420 planes");
    f.data.resize(5);check(!ToBgra(f),"short plane rejected");
    f.width=8193;check(!ToBgra(f),"oversize rejected");
    f.width=3;check(!ToBgra(f),"odd YUV rejected");
    f.width=1;f.height=1;f.format=PixelFormat::RGBA;f.data={10,20,30,40};
    p=ToBgra(f);check(p && p->data==std::vector<uint8_t>({30,20,10,255}),"RGBA channel order");
    AudioFrame a;a.format=AudioSampleFormat::S16;a.channels=1;a.data={0,128,255,127};check(Peak(a)==1.f,"negative full scale");
    a.data={0};check(Peak(a)==0,"truncated PCM rejected");
    VideoMailbox mailbox;std::thread writer([&]{for(int i=0;i<1000;++i)mailbox.Put(f);});
    for(int i=0;i<1000;++i){auto current=mailbox.Get();check(!current||current->data.size()==4,"mailbox ownership");}
    writer.join();check(bool(mailbox.Get()),"latest frame retained");mailbox.Clear();check(!mailbox.Get(),"mailbox clear");
    return failed?1:0;
}
