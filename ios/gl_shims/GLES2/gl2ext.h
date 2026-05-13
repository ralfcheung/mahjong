// iOS compatibility shim: redirect GLES2 extension headers to Apple's OpenGLES framework
#include <OpenGLES/ES2/glext.h>

// Apple's glext.h defines GL_OES_vertex_array_object (the extension token) but does
// NOT provide the associated function pointer typedefs that rlgl.h needs at line 1126.
// Define them unconditionally.
typedef void (*PFNGLGENVERTEXARRAYSOESPROC)(GLsizei n, GLuint *arrays);
typedef void (*PFNGLBINDVERTEXARRAYOESPROC)(GLuint array);
typedef void (*PFNGLDELETEVERTEXARRAYSOESPROC)(GLsizei n, const GLuint *arrays);

typedef void (*PFNGLDRAWARRAYSINSTANCEDEXTPROC)(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
typedef void (*PFNGLDRAWELEMENTSINSTANCEDEXTPROC)(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);
typedef void (*PFNGLVERTEXATTRIBDIVISOREXTPROC)(GLuint index, GLuint divisor);
