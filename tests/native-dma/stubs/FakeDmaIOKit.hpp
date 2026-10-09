// SPDX-License-Identifier: MIT
// Host-only doubles for the actual DmaBuffer.cpp. No Radeon, DMA or firmware.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#ifndef NAVI48_DMA_SHARED_IOKIT
using UInt8 = uint8_t; using UInt16 = uint16_t; using UInt32 = uint32_t; using UInt64 = uint64_t;
using IOReturn = uint32_t; using IOOptionBits = uint32_t;
using vm_size_t = uint64_t; using vm_offset_t = uint64_t;
using task_t = void *;
constexpr IOReturn kIOReturnSuccess=0, kIOReturnNotReady=1, kIOReturnBadArgument=2,
 kIOReturnNoMemory=3, kIOReturnIOError=4, kIOReturnNotPermitted=5;
constexpr IOOptionBits kIODirectionIn=1, kIODirectionOut=2, kIODirectionInOut=3;
inline int taskTag; inline task_t kernel_task=&taskTag;
#endif
namespace fake {
enum class Error { None, Allocate, Length, MemoryPrepare, Command, Assign, Prepare, Range,
 Generate, Cursor, Count, Zero, Alignment, PageLength, Limit, Alias, Sync, Transfer, Clear, Complete };
inline Error error=Error::None;
inline bool deviceMapper=true, discontiguous=false, differentDescriptor=false;
inline UInt8 addressBits=0;
inline unsigned live=0, physicalReads=0, prepares=0, clears=0, completes=0, allocations=0;
inline uint32_t syncDirection=0;
inline void (*callback)()=nullptr;
inline void (*afterGenerate)()=nullptr;
inline std::vector<uint8_t> gpuBytes;
inline uint64_t dmaBase=0x0000100000000000ULL; // intentionally NOT CPU physical
}
#ifndef NAVI48_DMA_SHARED_IOKIT
class OSObject {
 unsigned references_=1;
public:
 OSObject(){++fake::live;} virtual ~OSObject(){--fake::live;}
 void retain(){++references_;} void release(){if(!--references_)delete this;}
 unsigned references()const{return references_;}
};
class IOWorkLoop:public OSObject { public: bool gated=false; bool inGate()const{return gated;} };
class IOService:public OSObject {
public:
 IOService *provider=nullptr; IOWorkLoop *loop=nullptr; bool inactive=false;
 IOService *getProvider()const{return provider;} IOWorkLoop *getWorkLoop()const{return loop;}
 bool isInactive()const{return inactive;}
};
class IOPCIDevice:public IOService {
public:
 IOService *client=nullptr; uint16_t device=0x7550; unsigned reads=0;
 bool isOpen(IOService *owner)const{return owner==client;}
 uint16_t configRead16(uint8_t reg){++reads;switch(reg){case 0:return 0x1002;case 2:return device;
 case 0x2c:return 0x1849;case 0x2e:return 0x5417;default:return 0;}}
 uint32_t configRead32(uint8_t){++reads;return 0x030000c0;}
};
#endif
class IOMapper:public OSObject {
public:
 static IOMapper *copyMapperForDevice(IOService *){return fake::deviceMapper?new IOMapper:nullptr;}
};
class IOBufferMemoryDescriptor:public OSObject {
public:
 std::vector<uint8_t> data; bool prepared=false;
 explicit IOBufferMemoryDescriptor(uint64_t bytes):data(static_cast<size_t>(bytes),0xa5){}
 static IOBufferMemoryDescriptor *inTaskWithOptions(task_t task,IOOptionBits flags,vm_size_t size,vm_offset_t align){
  ++fake::allocations;
  if(task!=kernel_task||flags!=kIODirectionInOut||align!=4096||fake::error==fake::Error::Allocate)return nullptr;
  return new IOBufferMemoryDescriptor(size);
 }
 uint64_t getLength()const{return data.size()+(fake::error==fake::Error::Length?1:0);}
 void *getBytesNoCopy(){return data.data();}
 IOReturn prepare(IOOptionBits){if(fake::error==fake::Error::MemoryPrepare)return kIOReturnIOError;prepared=true;return 0;}
 IOReturn complete(IOOptionBits){++fake::completes;if(fake::error==fake::Error::Complete)return kIOReturnIOError;prepared=false;return 0;}
 uint64_t writeBytes(uint64_t off,const void *src,uint64_t bytes){
  if(fake::error==fake::Error::Transfer)return 0;
  std::memcpy(data.data()+off,src,static_cast<size_t>(bytes));return bytes;
 }
 uint64_t readBytes(uint64_t off,void *dst,uint64_t bytes){
  if(fake::error==fake::Error::Transfer)return 0;
  std::memcpy(dst,data.data()+off,static_cast<size_t>(bytes));return bytes;
 }
 // Deliberately no getPhysicalSegment: production code must not use it for DMA.
};
class IODMACommand:public OSObject {
 IOBufferMemoryDescriptor *memory_=nullptr; bool prepared_=false;
public:
 struct Segment64 { UInt64 fIOVMAddr, fLength; };
 using SegmentFunction=bool(*)(IODMACommand *,Segment64,void *,UInt32);
 enum MappingOptions{kMapped=0};
 static bool OutputHost64(IODMACommand *,Segment64,void *,UInt32){return true;}
 static IODMACommand *withSpecification(SegmentFunction function,UInt8 bits,UInt64 segment,MappingOptions map,
  UInt64 transfer,UInt32 align,IOMapper *mapper=nullptr,void *ref=nullptr){
  if(function!=OutputHost64||(bits!=48&&bits!=64)||segment!=4096||map!=kMapped||transfer||align!=4096||ref||
   bool(mapper)!=fake::deviceMapper||fake::error==fake::Error::Command)return nullptr;
  fake::addressBits=bits;return new IODMACommand;
 }
 IOReturn setMemoryDescriptor(IOBufferMemoryDescriptor *memory,bool autoPrepare){
  if(autoPrepare||fake::error==fake::Error::Assign)return kIOReturnIOError;
  memory_=memory;memory_->retain();return 0;
 }
 IOReturn prepare(UInt64 offset,UInt64 bytes){
  ++fake::prepares;
  if(fake::callback)fake::callback();
  if(offset||bytes!=memory_->data.size()||fake::error==fake::Error::Prepare)return kIOReturnIOError;
  fake::gpuBytes=memory_->data;prepared_=true;return 0;
 }
 IOReturn getPreparedOffsetAndLength(UInt64 *offset,UInt64 *bytes){
  if(!prepared_)return kIOReturnNotReady;
  *offset=fake::error==fake::Error::Range?4096:0;*bytes=memory_->data.size();return 0;
 }
 const IOBufferMemoryDescriptor *getMemoryDescriptor()const{return memory_;}
 IOBufferMemoryDescriptor *getIOMemoryDescriptor()const{return fake::differentDescriptor?nullptr:memory_;}
 IOReturn gen64IOVMSegments(UInt64 *offset,Segment64 *pages,UInt32 *count){
  if(fake::error==fake::Error::Generate)return kIOReturnIOError;
  for(uint32_t i=0;i<*count;++i)pages[i]={fake::dmaBase+uint64_t{i}*(fake::discontiguous?8192:4096),4096};
  switch(fake::error){
  case fake::Error::Zero:pages[0].fIOVMAddr=0;break;
  case fake::Error::Alignment:++pages[0].fIOVMAddr;break;
  case fake::Error::PageLength:pages[0].fLength=2048;break;
  case fake::Error::Limit:pages[0].fIOVMAddr=uint64_t{1}<<48;break;
  case fake::Error::Alias:if(*count>1)pages[1].fIOVMAddr=pages[0].fIOVMAddr;break;
  default:break;
  }
  *offset=memory_->data.size()-(fake::error==fake::Error::Cursor?4096:0);
  if(fake::error==fake::Error::Count)--*count;
  if(fake::afterGenerate)fake::afterGenerate();
  return 0;
 }
 IOReturn synchronize(IOOptionBits direction){
  fake::syncDirection=direction;
  if(fake::error==fake::Error::Sync)return kIOReturnIOError;
  if(direction==kIODirectionOut)fake::gpuBytes=memory_->data;
  else if(direction==kIODirectionIn)memory_->data=fake::gpuBytes;
  else return kIOReturnBadArgument;
  return 0;
 }
 IOReturn clearMemoryDescriptor(bool autoComplete){
  ++fake::clears;
  if(fake::callback)fake::callback();
  if(!autoComplete||fake::error==fake::Error::Clear)return kIOReturnIOError;
  prepared_=false;if(memory_)memory_->release();memory_=nullptr;return 0;
 }
};
#define kIODMACommandOutputHost64 (IODMACommand::OutputHost64)
inline bool OSCompareAndSwapPtr(void *oldValue,void *newValue,void *volatile *ptr){
 return __sync_bool_compare_and_swap(ptr,oldValue,newValue);
}
