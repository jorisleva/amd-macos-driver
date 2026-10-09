// SPDX-License-Identifier: MIT
#include "DmaBuffer.hpp"
#include <cstdio>
using n48native::DmaBuffer;
namespace {
unsigned checks=0,failed=0;
void check(bool value,const char *where){++checks;if(!value){++failed;std::fprintf(stderr,"FAIL: %s\n",where);}}
#define CHECK(value) check((value),#value)
struct Fixture {
 IOWorkLoop *loop=new IOWorkLoop;
 IOPCIDevice *pci=new IOPCIDevice;
 IOService *owner=new IOService;
 Fixture(){owner->provider=pci;owner->loop=loop;pci->client=owner;}
 ~Fixture(){owner->release();pci->release();loop->release();}
};
DmaBuffer *reentrant=nullptr;
void reenter(){CHECK(reentrant->release()==kIOReturnNotReady);CHECK(!reentrant->markPublished());}
void success(bool mapper,bool separatePages){
 fake::deviceMapper=mapper;fake::discontiguous=separatePages;
 const unsigned initial=fake::live;
 {
  Fixture f;DmaBuffer buffer;
  CHECK(buffer.allocate(f.owner,f.pci,f.loop,8192)==kIOReturnSuccess);
  CHECK(buffer.snapshot().phase==DmaBuffer::Phase::Prepared);
  CHECK(buffer.snapshot().deviceMapper==mapper);
  uint64_t page=99;
  CHECK(buffer.pageAddress(0,page)&&page==fake::dmaBase);
  CHECK(buffer.pageAddress(1,page)&&page==fake::dmaBase+(separatePages?8192:4096));
  CHECK(!buffer.pageAddress(2,page)&&page==0);
  CHECK(buffer.contiguousAddress(page)==!separatePages);
  CHECK(page==(separatePages?0:fake::dmaBase));
  const uint32_t pattern=0x13572468;
  CHECK(buffer.write(4096,&pattern,sizeof(pattern))==kIOReturnSuccess);
  CHECK(buffer.syncForDevice()==kIOReturnSuccess&&fake::syncDirection==kIODirectionOut);
  uint32_t value=0;std::memcpy(&value,fake::gpuBytes.data()+4096,4);
  CHECK(value==pattern); // fake DMA bounce buffer, NOT a GPU result
  value=0x24681357;std::memcpy(fake::gpuBytes.data()+4096,&value,4);
  CHECK(buffer.syncForCpu()==kIOReturnSuccess&&fake::syncDirection==kIODirectionIn);
  value=0;CHECK(buffer.read(4096,&value,4)==kIOReturnSuccess&&value==0x24681357);
  CHECK(buffer.write(UINT64_MAX,&pattern,4)==kIOReturnBadArgument);
  CHECK(buffer.read(8191,&value,4)==kIOReturnBadArgument);
  CHECK(buffer.write(0,nullptr,4)==kIOReturnBadArgument);
  CHECK(buffer.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnNotReady);
  CHECK(buffer.release()==kIOReturnSuccess);
  CHECK(buffer.snapshot().phase==DmaBuffer::Phase::Released);
  CHECK(!buffer.pageAddress(0,page)&&page==0);
 }
 CHECK(fake::live==initial);
 fake::deviceMapper=true;fake::discontiguous=false;
}
}
int main(){
 success(true,false);success(false,false);success(true,true);
 {
  Fixture f;
  for(uint64_t bytes:{uint64_t{0},uint64_t{1},uint64_t{4095},uint64_t{1048577}}){
   DmaBuffer buffer;const unsigned allocations=fake::allocations;
   CHECK(buffer.allocate(f.owner,f.pci,f.loop,bytes)==kIOReturnBadArgument);
   CHECK(fake::allocations==allocations);
  }
  {DmaBuffer b;f.loop->gated=true;CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnBadArgument);f.loop->gated=false;}
  {DmaBuffer b;f.pci->device=0x7551;CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnBadArgument);f.pci->device=0x7550;}
  {DmaBuffer b;f.pci->client=nullptr;CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnBadArgument);f.pci->client=f.owner;}
 }
 const unsigned initial=fake::live;
 for(auto error:{fake::Error::Allocate,fake::Error::Length,fake::Error::MemoryPrepare,fake::Error::Command,
  fake::Error::Assign,fake::Error::Prepare,fake::Error::Range,fake::Error::Generate,fake::Error::Cursor,
  fake::Error::Count,fake::Error::Zero,fake::Error::Alignment,fake::Error::PageLength,fake::Error::Limit,fake::Error::Alias}){
  fake::error=error;
  {Fixture f;DmaBuffer b;CHECK(b.allocate(f.owner,f.pci,f.loop,8192)!=kIOReturnSuccess);
   CHECK(b.snapshot().phase==DmaBuffer::Phase::Failed);uint64_t page=99;CHECK(!b.pageAddress(0,page)&&page==0);}
  CHECK(fake::live==initial);
 }
 fake::error=fake::Error::None;
 {
  Fixture f;DmaBuffer b;reentrant=&b;fake::callback=reenter;
  CHECK(b.allocate(f.owner,f.pci,f.loop,8192)==kIOReturnSuccess);
  CHECK(b.release()==kIOReturnSuccess);fake::callback=nullptr;reentrant=nullptr;
 }
 CHECK(fake::live==initial);
 {
  Fixture f;DmaBuffer b;CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnSuccess);
  fake::error=fake::Error::Sync;CHECK(b.syncForDevice()==kIOReturnIOError);
  CHECK(b.snapshot().phase==DmaBuffer::Phase::Failed);fake::error=fake::Error::None;
 }
 CHECK(fake::live==initial);
 {
  Fixture f;DmaBuffer b;CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnSuccess);
  f.pci->client=nullptr;uint64_t page=99;CHECK(!b.pageAddress(0,page)&&page==0);
  CHECK(b.release()==kIOReturnSuccess);
 }
 CHECK(fake::live==initial);
 // Deliberately retain published / uncertain-cleanup resources in a terminal
 // global quarantine. ASan can see the ownership root; no hot reuse is allowed.
 {
  Fixture f;DmaBuffer b;CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnSuccess);
  CHECK(b.markPublished());const unsigned clears=fake::clears,completes=fake::completes;
  CHECK(b.release()==kIOReturnNotReady&&b.snapshot().phase==DmaBuffer::Phase::Quarantined);
  CHECK(fake::clears==clears&&fake::completes==completes);
  CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnNotReady);
 }
 {
  Fixture f;DmaBuffer b;CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnSuccess);
  CHECK(b.markPublished());const unsigned clears=fake::clears;
  fake::error=fake::Error::Sync;CHECK(b.syncForCpu()==kIOReturnIOError);
  CHECK(b.snapshot().phase==DmaBuffer::Phase::Quarantined&&fake::clears==clears);
  fake::error=fake::Error::None;
 }
 {
  Fixture f;DmaBuffer b;CHECK(b.allocate(f.owner,f.pci,f.loop,4096)==kIOReturnSuccess);
  fake::error=fake::Error::Clear;CHECK(b.release()==kIOReturnIOError);
  CHECK(b.snapshot().phase==DmaBuffer::Phase::Quarantined);fake::error=fake::Error::None;
 }
 CHECK(fake::physicalReads==0);
 std::printf("native_dma_test: %u checks, %u failed; no hardware execution\n",checks,failed);
 return failed?1:0;
}
