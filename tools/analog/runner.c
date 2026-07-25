#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VKC(x) do{ VkResult r=(x); if(r){printf("VK FAIL %d @%d\n",r,__LINE__);exit(1);} }while(0)

typedef struct { int stage, W, LINES, PXW; float div, x1, inv_ratio, line_adv, field_adv;
                 int pal, svideo; float k0, k1; } Push;

static uint32_t *slurp(const char*p,size_t*sz){FILE*f=fopen(p,"rb");if(!f){printf("no %s\n",p);exit(1);}
 fseek(f,0,SEEK_END);*sz=ftell(f);fseek(f,0,SEEK_SET);uint32_t*b=malloc(*sz);
 if(fread(b,1,*sz,f)!=*sz){printf("short read\n");exit(1);}fclose(f);return b;}

int main(int argc,char**argv){
  int PXW=atoi(argv[1]), LINES=atoi(argv[2]); float div=atof(argv[3]);
  float x1=atof(argv[4]), inv_ratio=atof(argv[5]), line_adv=atof(argv[6]);
  float field_adv=atof(argv[7]); int pal=atoi(argv[8]), svideo=atoi(argv[9]);
  float k0=(argc>10)?atof(argv[10]):0.f, k1=(argc>11)?atof(argv[11]):0.f;
  int W=(int)(PXW*div);

  size_t nsrc=(size_t)PXW*LINES, nsig=(size_t)W*LINES, nout=nsrc;
  float *srcbuf=malloc(nsrc*16);
  if(fread(srcbuf,16,nsrc,stdin)!=nsrc){printf("stdin short\n");return 1;}

  VkInstance inst; VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO};ai.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&ai;
  VKC(vkCreateInstance(&ici,0,&inst));
  uint32_t nd=1; VkPhysicalDevice pd; VKC(vkEnumeratePhysicalDevices(inst,&nd,&pd));
  VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd,&props);
  fprintf(stderr,"device: %s\n",props.deviceName);
  uint32_t nq=0; vkGetPhysicalDeviceQueueFamilyProperties(pd,&nq,0);
  VkQueueFamilyProperties*qf=malloc(nq*sizeof*qf); vkGetPhysicalDeviceQueueFamilyProperties(pd,&nq,qf);
  uint32_t qi=0; for(uint32_t i=0;i<nq;i++) if(qf[i].queueFlags&VK_QUEUE_COMPUTE_BIT){qi=i;break;}
  float prio=1; VkDeviceQueueCreateInfo qci={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex=qi;qci.queueCount=1;qci.pQueuePriorities=&prio;
  VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;
  VkDevice dev; VKC(vkCreateDevice(pd,&dci,0,&dev)); VkQueue q; vkGetDeviceQueue(dev,qi,0,&q);
  VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd,&mp);

  VkDeviceSize sz[4]={nsrc*16, nsig*8, nsig*8, nout*16};
  VkBuffer buf[4]; VkDeviceMemory mem[4]; void*map[4];
  for(int b=0;b<4;b++){
    VkBufferCreateInfo bci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bci.size=sz[b];
    bci.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; VKC(vkCreateBuffer(dev,&bci,0,&buf[b]));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev,buf[b],&mr);
    uint32_t mi=0; for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((mr.memoryTypeBits&(1u<<i))&&
      (mp.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
      (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){mi=i;break;}
    VkMemoryAllocateInfo mai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mai.allocationSize=mr.size;mai.memoryTypeIndex=mi;
    VKC(vkAllocateMemory(dev,&mai,0,&mem[b])); VKC(vkBindBufferMemory(dev,buf[b],mem[b],0));
    VKC(vkMapMemory(dev,mem[b],0,sz[b],0,&map[b])); memset(map[b],0,sz[b]);
  }
  memcpy(map[0],srcbuf,nsrc*16);

  VkDescriptorSetLayoutBinding db[4];
  for(int i=0;i<4;i++){db[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0};}
  VkDescriptorSetLayoutCreateInfo dlci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dlci.bindingCount=4;dlci.pBindings=db;
  VkDescriptorSetLayout dsl; VKC(vkCreateDescriptorSetLayout(dev,&dlci,0,&dsl));
  VkPushConstantRange pcr={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
  VkPipelineLayoutCreateInfo plci={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount=1;plci.pSetLayouts=&dsl;plci.pushConstantRangeCount=1;plci.pPushConstantRanges=&pcr;
  VkPipelineLayout pl; VKC(vkCreatePipelineLayout(dev,&plci,0,&pl));
  size_t spvsz; uint32_t*spv=slurp(getenv("SPV")?getenv("SPV"):"chain_ntsc.spv",&spvsz);
  VkShaderModuleCreateInfo smci={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smci.codeSize=spvsz;smci.pCode=spv;
  VkShaderModule sm; VKC(vkCreateShaderModule(dev,&smci,0,&sm));
  VkComputePipelineCreateInfo cpci={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  cpci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cpci.stage.module=sm;cpci.stage.pName="main";cpci.layout=pl;
  VkPipeline pipe; VKC(vkCreateComputePipelines(dev,0,1,&cpci,0,&pipe));
  VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4};
  VkDescriptorPoolCreateInfo dpci={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets=1;dpci.poolSizeCount=1;dpci.pPoolSizes=&ps;
  VkDescriptorPool dp; VKC(vkCreateDescriptorPool(dev,&dpci,0,&dp));
  VkDescriptorSetAllocateInfo dsai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool=dp;dsai.descriptorSetCount=1;dsai.pSetLayouts=&dsl;
  VkDescriptorSet ds; VKC(vkAllocateDescriptorSets(dev,&dsai,&ds));
  VkDescriptorBufferInfo bi[4]; VkWriteDescriptorSet w[4]={0};
  for(int i=0;i<4;i++){bi[i]=(VkDescriptorBufferInfo){buf[i],0,sz[i]};
    w[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[i].dstSet=ds;w[i].dstBinding=i;
    w[i].descriptorCount=1;w[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[i].pBufferInfo=&bi[i];}
  vkUpdateDescriptorSets(dev,4,w,0,0);
  VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpi.queueFamilyIndex=qi;
  VkCommandPool cp; VKC(vkCreateCommandPool(dev,&cpi,0,&cp));
  VkCommandBufferAllocateInfo cbai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool=cp;cbai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cbai.commandBufferCount=1;
  VkCommandBuffer cb; VKC(vkAllocateCommandBuffers(dev,&cbai,&cb));
  VkCommandBufferBeginInfo cbbi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  cbbi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VKC(vkBeginCommandBuffer(cb,&cbbi));
  vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipe);
  vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&ds,0,0);
  VkMemoryBarrier mb={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  for(int s=0;s<4;s++){
    Push pc={s,W,LINES,PXW,div,x1,inv_ratio,line_adv,field_adv,pal,svideo,k0,k1};
    uint32_t n = (s==3) ? (uint32_t)nout : (uint32_t)nsig;
    vkCmdPushConstants(cb,pl,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push),&pc);
    vkCmdDispatch(cb,(n+63)/64,1,1);
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,0,0,0);
  }
  VKC(vkEndCommandBuffer(cb));
  VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
  VKC(vkQueueSubmit(q,1,&si,0)); VKC(vkQueueWaitIdle(q));
  {int ob=getenv("DUMP")?atoi(getenv("DUMP")):3;
    if(ob==3) fwrite(map[3],16,nout,stdout);
    else fwrite(map[ob],8,nsig,stdout);}
  return 0;
}
