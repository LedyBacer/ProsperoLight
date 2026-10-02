// SPDX-License-Identifier: MIT
#include "pyrowave_renderer.hpp"
#include "native_agc_present.hpp"
#include "moonlight_stream_keyboard.hpp"
#include "yuv_spirv.hpp"
void PyroWaveRenderer::initialize(VulkanContext &c, const Output &output, VkFormat format,
                                  const std::vector<VkImageView> &targets) {
    shutdown(); c_=&c;
    VkDescriptorSetLayoutBinding bindings[4]={};
    for(unsigned p=0;p<3;++p) bindings[p]={p,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo dl={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};bindings[3]={3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    dl.bindingCount=4;dl.pBindings=bindings;
    VK_OK(vkCreateDescriptorSetLayout(c.device,&dl,nullptr,&descriptors_));
    VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(PlaneConversion)};
    VkPipelineLayoutCreateInfo li={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&descriptors_;li.pushConstantRangeCount=1;li.pPushConstantRanges=&push;
    VK_OK(vkCreatePipelineLayout(c.device,&li,nullptr,&layout_));
    VkDescriptorPoolSize ps[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo dpi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpi.maxSets=1;dpi.poolSizeCount=2;dpi.pPoolSizes=ps;
    VK_OK(vkCreateDescriptorPool(c.device,&dpi,nullptr,&pool_));
    VkDescriptorSetAllocateInfo dai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};dai.descriptorPool=pool_;dai.descriptorSetCount=1;dai.pSetLayouts=&descriptors_;
    VK_OK(vkAllocateDescriptorSets(c.device,&dai,&set_));
    VkSamplerCreateInfo si={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;si.maxLod=0;
    VK_OK(vkCreateSampler(c.device,&si,nullptr,&sampler_));
    VkDescriptorImageInfo infos[3]={};VkWriteDescriptorSet writes[3]={};
    for(unsigned p=0;p<3;++p) {
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=output.planes[p].image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
        vi.format=output.views.planes[p].view_format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VK_OK(vkCreateImageView(c.device,&vi,nullptr,&planes_[p]));
        infos[p]={sampler_,planes_[p],VK_IMAGE_LAYOUT_GENERAL};writes[p].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[p].dstSet=set_;writes[p].dstBinding=p;writes[p].descriptorCount=1;writes[p].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[p].pImageInfo=&infos[p];
    }
    vkUpdateDescriptorSets(c.device,3,writes,0,nullptr);
    VkBufferCreateInfo buffer_info={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size=(1+72*6+1+72*8)*sizeof(uint32_t);buffer_info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VK_OK(vkCreateBuffer(c.device,&buffer_info,nullptr,&hud_buffer_));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(c.device,hud_buffer_,&req);
    VkMemoryAllocateInfo alloc={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;
    alloc.memoryTypeIndex=c.memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    hud_coherent_=(c.memory.memoryTypes[alloc.memoryTypeIndex].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
    VK_OK(vkAllocateMemory(c.device,&alloc,nullptr,&hud_memory_));
    VK_OK(vkBindBufferMemory(c.device,hud_buffer_,hud_memory_,0));
    VK_OK(vkMapMemory(c.device,hud_memory_,0,VK_WHOLE_SIZE,0,reinterpret_cast<void **>(&hud_mapped_)));
    memset(hud_mapped_,0,buffer_info.size);
    VkDescriptorBufferInfo buffer_descriptor={hud_buffer_,0,buffer_info.size};
    VkWriteDescriptorSet hud_write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};hud_write.dstSet=set_;hud_write.dstBinding=3;
    hud_write.descriptorCount=1;hud_write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;hud_write.pBufferInfo=&buffer_descriptor;
    vkUpdateDescriptorSets(c.device,1,&hud_write,0,nullptr);
    update_hud("PyroWave: waiting for stream statistics",false);
    VkAttachmentDescription attachment={};attachment.format=format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub={};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
    VkSubpassDependency dependency={};dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;
    dependency.srcStageMask=dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ri={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ri.attachmentCount=1;ri.pAttachments=&attachment;ri.subpassCount=1;ri.pSubpasses=&sub;ri.dependencyCount=1;ri.pDependencies=&dependency;
    VK_OK(vkCreateRenderPass(c.device,&ri,nullptr,&pass_));
    VkShaderModule shaders[2]={};const uint32_t *code[]={yuv_vert,yuv_frag};size_t bytes[]={sizeof(yuv_vert),sizeof(yuv_frag)};
    VkPipelineShaderStageCreateInfo stages[2]={};
    for(unsigned i=0;i<2;++i){VkShaderModuleCreateInfo sm={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=bytes[i];sm.pCode=code[i];VK_OK(vkCreateShaderModule(c.device,&sm,nullptr,&shaders[i]));stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=shaders[i];stages[i].pName="main";}
    VkPipelineVertexInputStateCreateInfo vertex={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport={0,0,3840,2160,0,1};VkRect2D scissor={{0,0},{3840,2160}};
    VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=1;vp.pViewports=&viewport;vp.scissorCount=1;vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend={};blend.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo cb={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&blend;
    VkGraphicsPipelineCreateInfo pi={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=2;pi.pStages=stages;pi.pVertexInputState=&vertex;pi.pInputAssemblyState=&assembly;pi.pViewportState=&vp;pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;pi.pColorBlendState=&cb;pi.layout=layout_;pi.renderPass=pass_;
    VK_OK(vkCreateGraphicsPipelines(c.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline_));
    for(auto module:shaders)vkDestroyShaderModule(c.device,module,nullptr);
    for(auto view:targets){VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=pass_;fi.attachmentCount=1;fi.pAttachments=&view;fi.width=3840;fi.height=2160;fi.layers=1;VkFramebuffer fb;VK_OK(vkCreateFramebuffer(c.device,&fi,nullptr,&fb));framebuffers_.push_back(fb);}
}
void PyroWaveRenderer::render(VkCommandBuffer cmd,const Output &output,unsigned target) {
    VkImageMemoryBarrier barriers[3]={};
    for(unsigned p=0;p<3;++p){auto &b=barriers[p];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.image=output.planes[p].image;b.oldLayout=b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,3,barriers);
    VkRenderPassBeginInfo begin={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass_;begin.framebuffer=framebuffers_.at(target);begin.renderArea={{0,0},{3840,2160}};
    vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout_,0,1,&set_,0,nullptr);
    vkCmdPushConstants(cmd,layout_,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(conversion),&conversion);vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderPass(cmd);
}
void PyroWaveRenderer::update_hud(const char *text, bool enabled) {
    if(!hud_mapped_)return;
    hud_mapped_[0]=enabled?1:0;
    if(text){
        std::fill(hud_mapped_+1,hud_mapped_+1+72*6,32u);
        unsigned row=0,column=0;
        for(const unsigned char *p=reinterpret_cast<const unsigned char *>(text);*p && row<6;++p){
            if(*p=='\n'){++row;column=0;continue;}
            if(column<72)hud_mapped_[1+row*72+column++]=*p>=32 && *p<128?*p:32;
        }
    }
    // Reuse native input state/key labels; only the overlay presenter differs.
    int keyboard=0,shifted=0;uint32_t selected=0;
    native_agc_keyboard_snapshot(&keyboard,&selected,&shifted);
    hud_mapped_[1+72*6]=keyboard ? 1u : 0u;
    if(keyboard) {
        uint32_t *keys=hud_mapped_+1+72*6+1;
        std::fill(keys,keys+72*8,32u);
        auto write=[&](unsigned row,unsigned column,const char *label) {
            for(const unsigned char *p=reinterpret_cast<const unsigned char *>(label);*p && column<72;++p)
                keys[row*72+column++]=*p;
        };
        write(0,0,"ProsperoLight keyboard: selected key in brackets");
        write(1,0,"Cross: type  Square: backspace  Triangle: shift  Options: enter");
        write(2,0,"Circle: close   D-pad: select key");
        for(unsigned row=0;row<moonlight_keyboard_row_count;++row) {
            const unsigned start=moonlight_keyboard_row_offsets[row];
            const unsigned end=moonlight_keyboard_row_offsets[row+1];
            const unsigned cell=72/(end-start);
            for(unsigned index=start;index<end;++index) {
                const char *label=moonlight_keyboard_label(index,shifted!=0);
                char text[16];snprintf(text,sizeof(text),index==selected ? "[%s]" : " %s ",label);
                write(row+3,(index-start)*cell,text);
            }
        }
    }
    if(!hud_coherent_){VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=hud_memory_;range.size=VK_WHOLE_SIZE;VK_OK(vkFlushMappedMemoryRanges(c_->device,1,&range));}
}
void PyroWaveRenderer::shutdown() {
    if(!c_)return;auto d=c_->device;
    for(auto fb:framebuffers_)vkDestroyFramebuffer(d,fb,nullptr);framebuffers_.clear();
    if(hud_mapped_)vkUnmapMemory(d,hud_memory_);
    if(hud_buffer_)vkDestroyBuffer(d,hud_buffer_,nullptr);
    if(hud_memory_)vkFreeMemory(d,hud_memory_,nullptr);
    hud_mapped_=nullptr;hud_buffer_=VK_NULL_HANDLE;hud_memory_=VK_NULL_HANDLE;
    if(pipeline_)vkDestroyPipeline(d,pipeline_,nullptr);if(pass_)vkDestroyRenderPass(d,pass_,nullptr);
    if(layout_)vkDestroyPipelineLayout(d,layout_,nullptr);if(pool_)vkDestroyDescriptorPool(d,pool_,nullptr);
    for(auto view:planes_)if(view)vkDestroyImageView(d,view,nullptr);
    if(sampler_)vkDestroySampler(d,sampler_,nullptr);if(descriptors_)vkDestroyDescriptorSetLayout(d,descriptors_,nullptr);
    pipeline_=VK_NULL_HANDLE;pass_=VK_NULL_HANDLE;layout_=VK_NULL_HANDLE;pool_=VK_NULL_HANDLE;sampler_=VK_NULL_HANDLE;descriptors_=VK_NULL_HANDLE;for(auto &view:planes_)view=VK_NULL_HANDLE;
    c_=nullptr;
}
