#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VKC(x) do { VkResult r = (x); if (r) { printf("VK FAIL %d @%d\n", r, __LINE__); exit(1); } } while (0)

typedef struct
{
   int stage, W, LINES, PXW;
   float div, x1, inv_ratio, line_adv, field_adv;
   int pal, svideo;
   float k0, k1;
} Push;

static uint32_t *slurp(const char *p, size_t *sz)
{
   uint32_t *b;
   FILE *f = fopen(p, "rb");
   if (!f)
   {
      printf("no %s\n", p);
      exit(1);
   }
   fseek(f, 0, SEEK_END);
   *sz = ftell(f);
   fseek(f, 0, SEEK_SET);
   b = (uint32_t*)malloc(*sz);
   if (fread(b, 1, *sz, f) != *sz)
   {
      printf("short read\n");
      exit(1);
   }
   fclose(f);
   return b;
}

int main(int argc, char **argv)
{
   int PXW, LINES, W, pal, svideo, i, b, s, ob;
   float div, x1, inv_ratio, line_adv, field_adv, k0, k1, prio;
   size_t nsrc, nsig, nout, spvsz;
   float *srcbuf;
   uint32_t *spv;
   uint32_t nd, nq, qi, mi, n;
   VkInstance inst;
   VkApplicationInfo ai;
   VkInstanceCreateInfo ici;
   VkPhysicalDevice pd;
   VkPhysicalDeviceProperties props;
   VkQueueFamilyProperties *qf;
   VkDeviceQueueCreateInfo qci;
   VkDeviceCreateInfo dci;
   VkDevice dev;
   VkQueue q;
   VkPhysicalDeviceMemoryProperties mp;
   VkDeviceSize sz[4];
   VkBuffer buf[4];
   VkDeviceMemory mem[4];
   void *map[4];
   VkBufferCreateInfo bci;
   VkMemoryRequirements mr;
   VkMemoryAllocateInfo mai;
   VkDescriptorSetLayoutBinding db[4];
   VkDescriptorSetLayoutCreateInfo dlci;
   VkDescriptorSetLayout dsl;
   VkPushConstantRange pcr;
   VkPipelineLayoutCreateInfo plci;
   VkPipelineLayout pl;
   VkShaderModuleCreateInfo smci;
   VkShaderModule sm;
   VkComputePipelineCreateInfo cpci;
   VkPipeline pipe;
   VkDescriptorPoolSize ps;
   VkDescriptorPoolCreateInfo dpci;
   VkDescriptorPool dp;
   VkDescriptorSetAllocateInfo dsai;
   VkDescriptorSet ds;
   VkDescriptorBufferInfo bi[4];
   VkWriteDescriptorSet w[4];
   VkCommandPoolCreateInfo cpi;
   VkCommandPool cp;
   VkCommandBufferAllocateInfo cbai;
   VkCommandBuffer cb;
   VkCommandBufferBeginInfo cbbi;
   VkMemoryBarrier mb;
   VkSubmitInfo si;
   Push pc;
   const VkMemoryPropertyFlags host =
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

   PXW       = atoi(argv[1]);
   LINES     = atoi(argv[2]);
   div       = (float)atof(argv[3]);
   x1        = (float)atof(argv[4]);
   inv_ratio = (float)atof(argv[5]);
   line_adv  = (float)atof(argv[6]);
   field_adv = (float)atof(argv[7]);
   pal       = atoi(argv[8]);
   svideo    = atoi(argv[9]);
   k0        = (argc > 10) ? (float)atof(argv[10]) : 0.f;
   k1        = (argc > 11) ? (float)atof(argv[11]) : 0.f;
   W         = (int)(PXW * div);

   nsrc = (size_t)PXW * LINES;
   nsig = (size_t)W * LINES;
   nout = nsrc;
   srcbuf = (float*)malloc(nsrc * 16);
   if (fread(srcbuf, 16, nsrc, stdin) != nsrc)
   {
      printf("stdin short\n");
      return 1;
   }

   memset(&ai, 0, sizeof(ai));
   ai.sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   ai.apiVersion = VK_API_VERSION_1_1;
   memset(&ici, 0, sizeof(ici));
   ici.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
   ici.pApplicationInfo = &ai;
   VKC(vkCreateInstance(&ici, 0, &inst));
   nd = 1;
   VKC(vkEnumeratePhysicalDevices(inst, &nd, &pd));
   vkGetPhysicalDeviceProperties(pd, &props);
   fprintf(stderr, "device: %s\n", props.deviceName);
   nq = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, 0);
   qf = (VkQueueFamilyProperties*)malloc(nq * sizeof(*qf));
   vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf);
   qi = 0;
   for (i = 0; i < (int)nq; i++)
      if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
      {
         qi = (uint32_t)i;
         break;
      }
   prio = 1;
   memset(&qci, 0, sizeof(qci));
   qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   qci.queueFamilyIndex = qi;
   qci.queueCount       = 1;
   qci.pQueuePriorities = &prio;
   memset(&dci, 0, sizeof(dci));
   dci.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   dci.queueCreateInfoCount = 1;
   dci.pQueueCreateInfos    = &qci;
   VKC(vkCreateDevice(pd, &dci, 0, &dev));
   vkGetDeviceQueue(dev, qi, 0, &q);
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);

   sz[0] = nsrc * 16;
   sz[1] = nsig * 8;
   sz[2] = nsig * 8;
   sz[3] = nout * 16;
   for (b = 0; b < 4; b++)
   {
      memset(&bci, 0, sizeof(bci));
      bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bci.size  = sz[b];
      bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      VKC(vkCreateBuffer(dev, &bci, 0, &buf[b]));
      vkGetBufferMemoryRequirements(dev, buf[b], &mr);
      mi = 0;
      for (i = 0; i < (int)mp.memoryTypeCount; i++)
         if ((mr.memoryTypeBits & (1u << i)) &&
             (mp.memoryTypes[i].propertyFlags & host) == host)
         {
            mi = (uint32_t)i;
            break;
         }
      memset(&mai, 0, sizeof(mai));
      mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      mai.allocationSize  = mr.size;
      mai.memoryTypeIndex = mi;
      VKC(vkAllocateMemory(dev, &mai, 0, &mem[b]));
      VKC(vkBindBufferMemory(dev, buf[b], mem[b], 0));
      VKC(vkMapMemory(dev, mem[b], 0, sz[b], 0, &map[b]));
      memset(map[b], 0, sz[b]);
   }
   memcpy(map[0], srcbuf, nsrc * 16);

   for (i = 0; i < 4; i++)
   {
      db[i].binding            = (uint32_t)i;
      db[i].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      db[i].descriptorCount    = 1;
      db[i].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
      db[i].pImmutableSamplers = 0;
   }
   memset(&dlci, 0, sizeof(dlci));
   dlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dlci.bindingCount = 4;
   dlci.pBindings    = db;
   VKC(vkCreateDescriptorSetLayout(dev, &dlci, 0, &dsl));
   pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
   pcr.offset     = 0;
   pcr.size       = sizeof(Push);
   memset(&plci, 0, sizeof(plci));
   plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   plci.setLayoutCount         = 1;
   plci.pSetLayouts            = &dsl;
   plci.pushConstantRangeCount = 1;
   plci.pPushConstantRanges    = &pcr;
   VKC(vkCreatePipelineLayout(dev, &plci, 0, &pl));
   spv = slurp(getenv("SPV") ? getenv("SPV") : "chain_ntsc.spv", &spvsz);
   memset(&smci, 0, sizeof(smci));
   smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   smci.codeSize = spvsz;
   smci.pCode    = spv;
   VKC(vkCreateShaderModule(dev, &smci, 0, &sm));
   memset(&cpci, 0, sizeof(cpci));
   cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
   cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
   cpci.stage.module = sm;
   cpci.stage.pName  = "main";
   cpci.layout       = pl;
   VKC(vkCreateComputePipelines(dev, 0, 1, &cpci, 0, &pipe));
   ps.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   ps.descriptorCount = 4;
   memset(&dpci, 0, sizeof(dpci));
   dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   dpci.maxSets       = 1;
   dpci.poolSizeCount = 1;
   dpci.pPoolSizes    = &ps;
   VKC(vkCreateDescriptorPool(dev, &dpci, 0, &dp));
   memset(&dsai, 0, sizeof(dsai));
   dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   dsai.descriptorPool     = dp;
   dsai.descriptorSetCount = 1;
   dsai.pSetLayouts        = &dsl;
   VKC(vkAllocateDescriptorSets(dev, &dsai, &ds));
   memset(w, 0, sizeof(w));
   for (i = 0; i < 4; i++)
   {
      bi[i].buffer         = buf[i];
      bi[i].offset         = 0;
      bi[i].range          = sz[i];
      w[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w[i].dstSet          = ds;
      w[i].dstBinding      = (uint32_t)i;
      w[i].descriptorCount = 1;
      w[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[i].pBufferInfo     = &bi[i];
   }
   vkUpdateDescriptorSets(dev, 4, w, 0, 0);
   memset(&cpi, 0, sizeof(cpi));
   cpi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cpi.queueFamilyIndex = qi;
   VKC(vkCreateCommandPool(dev, &cpi, 0, &cp));
   memset(&cbai, 0, sizeof(cbai));
   cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cbai.commandPool        = cp;
   cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cbai.commandBufferCount = 1;
   VKC(vkAllocateCommandBuffers(dev, &cbai, &cb));
   memset(&cbbi, 0, sizeof(cbbi));
   cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   VKC(vkBeginCommandBuffer(cb, &cbbi));
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
   vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, 0);
   memset(&mb, 0, sizeof(mb));
   mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
   mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
   mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   pc.W         = W;
   pc.LINES     = LINES;
   pc.PXW       = PXW;
   pc.div       = div;
   pc.x1        = x1;
   pc.inv_ratio = inv_ratio;
   pc.line_adv  = line_adv;
   pc.field_adv = field_adv;
   pc.pal       = pal;
   pc.svideo    = svideo;
   pc.k0        = k0;
   pc.k1        = k1;
   for (s = 0; s < 4; s++)
   {
      pc.stage = s;
      n = (s == 3) ? (uint32_t)nout : (uint32_t)nsig;
      vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &pc);
      vkCmdDispatch(cb, (n + 63) / 64, 1, 1);
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, 0, 0, 0);
   }
   VKC(vkEndCommandBuffer(cb));
   memset(&si, 0, sizeof(si));
   si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.commandBufferCount = 1;
   si.pCommandBuffers    = &cb;
   VKC(vkQueueSubmit(q, 1, &si, 0));
   VKC(vkQueueWaitIdle(q));
   ob = getenv("DUMP") ? atoi(getenv("DUMP")) : 3;
   if (ob == 3)
      fwrite(map[3], 16, nout, stdout);
   else
      fwrite(map[ob], 8, nsig, stdout);
   return 0;
}
