#version 450

#if defined(ATTACHMENT_0)
layout(set = 0, binding = 0, input_attachment_index = 0) uniform subpassInput uFramebuffer;
#elif defined(ATTACHMENT_1)
layout(set = 0, binding = 1, input_attachment_index = 1) uniform subpassInput uFramebuffer;
#endif

layout(location = 0) out vec4 FragColor;

void main()
{
	FragColor = subpassLoad(uFramebuffer);
}
