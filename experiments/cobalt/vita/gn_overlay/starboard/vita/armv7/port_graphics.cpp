/*
 * VitaGL-backed Starboard 15 EGL/GLES function tables.
 * Missing or incompatible functions remain null, never return fabricated success.
 * This is a partial adapter: it does NOT yet meet full Cobalt graphics requirements.
 */
#include <vitaGL.h>
#include "starboard/egl.h"
#include "starboard/gles.h"
#include <pthread.h>
#include <cstring>

// VitaGL's public header omits this exported EGL function declaration.
extern "C" EGLSurface eglGetCurrentSurface(EGLint readdraw);
namespace {
SbEglDisplay get_display(SbEglNativeDisplayType display) {
  return ::eglGetDisplay(reinterpret_cast<NativeDisplayType>(display));
}
SbEglSurface create_window(SbEglDisplay display,SbEglConfig config,
                           SbEglNativeWindowType window,
                           const SbEglInt32* attributes) {
  return ::eglCreateWindowSurface(display,config,
             reinterpret_cast<void*>(window),attributes);
}
SbEglInterface egl_api={};
SbGlesInterface gl_api={};
pthread_once_t once=PTHREAD_ONCE_INIT;
void set_interfaces() {
  egl_api.eglChooseConfig = reinterpret_cast<decltype(egl_api.eglChooseConfig)>(&::eglChooseConfig);
  egl_api.eglCreateContext = reinterpret_cast<decltype(egl_api.eglCreateContext)>(&::eglCreateContext);
  egl_api.eglCreateWindowSurface = &create_window;
  egl_api.eglDestroyContext = reinterpret_cast<decltype(egl_api.eglDestroyContext)>(&::eglDestroyContext);
  egl_api.eglDestroySurface = reinterpret_cast<decltype(egl_api.eglDestroySurface)>(&::eglDestroySurface);
  egl_api.eglGetConfigAttrib = reinterpret_cast<decltype(egl_api.eglGetConfigAttrib)>(&::eglGetConfigAttrib);
  egl_api.eglGetConfigs = reinterpret_cast<decltype(egl_api.eglGetConfigs)>(&::eglGetConfigs);
  egl_api.eglGetCurrentSurface = reinterpret_cast<decltype(egl_api.eglGetCurrentSurface)>(&::eglGetCurrentSurface);
  egl_api.eglGetDisplay = &get_display;
  egl_api.eglGetError = reinterpret_cast<decltype(egl_api.eglGetError)>(&::eglGetError);
  egl_api.eglGetProcAddress = reinterpret_cast<decltype(egl_api.eglGetProcAddress)>(&::eglGetProcAddress);
  egl_api.eglInitialize = reinterpret_cast<decltype(egl_api.eglInitialize)>(&::eglInitialize);
  egl_api.eglMakeCurrent = reinterpret_cast<decltype(egl_api.eglMakeCurrent)>(&::eglMakeCurrent);
  egl_api.eglQueryContext = reinterpret_cast<decltype(egl_api.eglQueryContext)>(&::eglQueryContext);
  egl_api.eglQueryString = reinterpret_cast<decltype(egl_api.eglQueryString)>(&::eglQueryString);
  egl_api.eglQuerySurface = reinterpret_cast<decltype(egl_api.eglQuerySurface)>(&::eglQuerySurface);
  egl_api.eglSwapBuffers = reinterpret_cast<decltype(egl_api.eglSwapBuffers)>(&::eglSwapBuffers);
  egl_api.eglTerminate = reinterpret_cast<decltype(egl_api.eglTerminate)>(&::eglTerminate);
  egl_api.eglSwapInterval = reinterpret_cast<decltype(egl_api.eglSwapInterval)>(&::eglSwapInterval);
  egl_api.eglBindAPI = reinterpret_cast<decltype(egl_api.eglBindAPI)>(&::eglBindAPI);
  egl_api.eglQueryAPI = reinterpret_cast<decltype(egl_api.eglQueryAPI)>(&::eglQueryAPI);
  egl_api.eglGetCurrentContext = reinterpret_cast<decltype(egl_api.eglGetCurrentContext)>(&::eglGetCurrentContext);
  gl_api.glActiveTexture = reinterpret_cast<decltype(gl_api.glActiveTexture)>(&::glActiveTexture);
  gl_api.glAttachShader = reinterpret_cast<decltype(gl_api.glAttachShader)>(&::glAttachShader);
  gl_api.glBindAttribLocation = reinterpret_cast<decltype(gl_api.glBindAttribLocation)>(&::glBindAttribLocation);
  gl_api.glBindBuffer = reinterpret_cast<decltype(gl_api.glBindBuffer)>(&::glBindBuffer);
  gl_api.glBindFramebuffer = reinterpret_cast<decltype(gl_api.glBindFramebuffer)>(&::glBindFramebuffer);
  gl_api.glBindRenderbuffer = reinterpret_cast<decltype(gl_api.glBindRenderbuffer)>(&::glBindRenderbuffer);
  gl_api.glBindTexture = reinterpret_cast<decltype(gl_api.glBindTexture)>(&::glBindTexture);
  gl_api.glBlendEquation = reinterpret_cast<decltype(gl_api.glBlendEquation)>(&::glBlendEquation);
  gl_api.glBlendEquationSeparate = reinterpret_cast<decltype(gl_api.glBlendEquationSeparate)>(&::glBlendEquationSeparate);
  gl_api.glBlendFunc = reinterpret_cast<decltype(gl_api.glBlendFunc)>(&::glBlendFunc);
  gl_api.glBlendFuncSeparate = reinterpret_cast<decltype(gl_api.glBlendFuncSeparate)>(&::glBlendFuncSeparate);
  gl_api.glBufferData = reinterpret_cast<decltype(gl_api.glBufferData)>(&::glBufferData);
  gl_api.glBufferSubData = reinterpret_cast<decltype(gl_api.glBufferSubData)>(&::glBufferSubData);
  gl_api.glCheckFramebufferStatus = reinterpret_cast<decltype(gl_api.glCheckFramebufferStatus)>(&::glCheckFramebufferStatus);
  gl_api.glClear = reinterpret_cast<decltype(gl_api.glClear)>(&::glClear);
  gl_api.glClearColor = reinterpret_cast<decltype(gl_api.glClearColor)>(&::glClearColor);
  gl_api.glClearDepthf = reinterpret_cast<decltype(gl_api.glClearDepthf)>(&::glClearDepthf);
  gl_api.glClearStencil = reinterpret_cast<decltype(gl_api.glClearStencil)>(&::glClearStencil);
  gl_api.glColorMask = reinterpret_cast<decltype(gl_api.glColorMask)>(&::glColorMask);
  gl_api.glCompileShader = reinterpret_cast<decltype(gl_api.glCompileShader)>(&::glCompileShader);
  gl_api.glCompressedTexImage2D = reinterpret_cast<decltype(gl_api.glCompressedTexImage2D)>(&::glCompressedTexImage2D);
  gl_api.glCopyTexImage2D = reinterpret_cast<decltype(gl_api.glCopyTexImage2D)>(&::glCopyTexImage2D);
  gl_api.glCopyTexSubImage2D = reinterpret_cast<decltype(gl_api.glCopyTexSubImage2D)>(&::glCopyTexSubImage2D);
  gl_api.glCreateProgram = reinterpret_cast<decltype(gl_api.glCreateProgram)>(&::glCreateProgram);
  gl_api.glCreateShader = reinterpret_cast<decltype(gl_api.glCreateShader)>(&::glCreateShader);
  gl_api.glCullFace = reinterpret_cast<decltype(gl_api.glCullFace)>(&::glCullFace);
  gl_api.glDeleteBuffers = reinterpret_cast<decltype(gl_api.glDeleteBuffers)>(&::glDeleteBuffers);
  gl_api.glDeleteFramebuffers = reinterpret_cast<decltype(gl_api.glDeleteFramebuffers)>(&::glDeleteFramebuffers);
  gl_api.glDeleteProgram = reinterpret_cast<decltype(gl_api.glDeleteProgram)>(&::glDeleteProgram);
  gl_api.glDeleteRenderbuffers = reinterpret_cast<decltype(gl_api.glDeleteRenderbuffers)>(&::glDeleteRenderbuffers);
  gl_api.glDeleteShader = reinterpret_cast<decltype(gl_api.glDeleteShader)>(&::glDeleteShader);
  gl_api.glDeleteTextures = reinterpret_cast<decltype(gl_api.glDeleteTextures)>(&::glDeleteTextures);
  gl_api.glDepthFunc = reinterpret_cast<decltype(gl_api.glDepthFunc)>(&::glDepthFunc);
  gl_api.glDepthMask = reinterpret_cast<decltype(gl_api.glDepthMask)>(&::glDepthMask);
  gl_api.glDepthRangef = reinterpret_cast<decltype(gl_api.glDepthRangef)>(&::glDepthRangef);
  gl_api.glDisable = reinterpret_cast<decltype(gl_api.glDisable)>(&::glDisable);
  gl_api.glDisableVertexAttribArray = reinterpret_cast<decltype(gl_api.glDisableVertexAttribArray)>(&::glDisableVertexAttribArray);
  gl_api.glDrawArrays = reinterpret_cast<decltype(gl_api.glDrawArrays)>(&::glDrawArrays);
  gl_api.glDrawElements = reinterpret_cast<decltype(gl_api.glDrawElements)>(&::glDrawElements);
  gl_api.glEnable = reinterpret_cast<decltype(gl_api.glEnable)>(&::glEnable);
  gl_api.glEnableVertexAttribArray = reinterpret_cast<decltype(gl_api.glEnableVertexAttribArray)>(&::glEnableVertexAttribArray);
  gl_api.glFinish = reinterpret_cast<decltype(gl_api.glFinish)>(&::glFinish);
  gl_api.glFlush = reinterpret_cast<decltype(gl_api.glFlush)>(&::glFlush);
  gl_api.glFramebufferRenderbuffer = reinterpret_cast<decltype(gl_api.glFramebufferRenderbuffer)>(&::glFramebufferRenderbuffer);
  gl_api.glFramebufferTexture2D = reinterpret_cast<decltype(gl_api.glFramebufferTexture2D)>(&::glFramebufferTexture2D);
  gl_api.glFrontFace = reinterpret_cast<decltype(gl_api.glFrontFace)>(&::glFrontFace);
  gl_api.glGenBuffers = reinterpret_cast<decltype(gl_api.glGenBuffers)>(&::glGenBuffers);
  gl_api.glGenerateMipmap = reinterpret_cast<decltype(gl_api.glGenerateMipmap)>(&::glGenerateMipmap);
  gl_api.glGenFramebuffers = reinterpret_cast<decltype(gl_api.glGenFramebuffers)>(&::glGenFramebuffers);
  gl_api.glGenRenderbuffers = reinterpret_cast<decltype(gl_api.glGenRenderbuffers)>(&::glGenRenderbuffers);
  gl_api.glGenTextures = reinterpret_cast<decltype(gl_api.glGenTextures)>(&::glGenTextures);
  gl_api.glGetActiveAttrib = reinterpret_cast<decltype(gl_api.glGetActiveAttrib)>(&::glGetActiveAttrib);
  gl_api.glGetActiveUniform = reinterpret_cast<decltype(gl_api.glGetActiveUniform)>(&::glGetActiveUniform);
  gl_api.glGetAttachedShaders = reinterpret_cast<decltype(gl_api.glGetAttachedShaders)>(&::glGetAttachedShaders);
  gl_api.glGetAttribLocation = reinterpret_cast<decltype(gl_api.glGetAttribLocation)>(&::glGetAttribLocation);
  gl_api.glGetBooleanv = reinterpret_cast<decltype(gl_api.glGetBooleanv)>(&::glGetBooleanv);
  gl_api.glGetBufferParameteriv = reinterpret_cast<decltype(gl_api.glGetBufferParameteriv)>(&::glGetBufferParameteriv);
  gl_api.glGetError = reinterpret_cast<decltype(gl_api.glGetError)>(&::glGetError);
  gl_api.glGetFloatv = reinterpret_cast<decltype(gl_api.glGetFloatv)>(&::glGetFloatv);
  gl_api.glGetFramebufferAttachmentParameteriv = reinterpret_cast<decltype(gl_api.glGetFramebufferAttachmentParameteriv)>(&::glGetFramebufferAttachmentParameteriv);
  gl_api.glGetIntegerv = reinterpret_cast<decltype(gl_api.glGetIntegerv)>(&::glGetIntegerv);
  gl_api.glGetProgramiv = reinterpret_cast<decltype(gl_api.glGetProgramiv)>(&::glGetProgramiv);
  gl_api.glGetProgramInfoLog = reinterpret_cast<decltype(gl_api.glGetProgramInfoLog)>(&::glGetProgramInfoLog);
  gl_api.glGetShaderiv = reinterpret_cast<decltype(gl_api.glGetShaderiv)>(&::glGetShaderiv);
  gl_api.glGetShaderInfoLog = reinterpret_cast<decltype(gl_api.glGetShaderInfoLog)>(&::glGetShaderInfoLog);
  gl_api.glGetShaderSource = reinterpret_cast<decltype(gl_api.glGetShaderSource)>(&::glGetShaderSource);
  gl_api.glGetString = reinterpret_cast<decltype(gl_api.glGetString)>(&::glGetString);
  gl_api.glGetUniformLocation = reinterpret_cast<decltype(gl_api.glGetUniformLocation)>(&::glGetUniformLocation);
  gl_api.glGetVertexAttribfv = reinterpret_cast<decltype(gl_api.glGetVertexAttribfv)>(&::glGetVertexAttribfv);
  gl_api.glGetVertexAttribiv = reinterpret_cast<decltype(gl_api.glGetVertexAttribiv)>(&::glGetVertexAttribiv);
  gl_api.glGetVertexAttribPointerv = reinterpret_cast<decltype(gl_api.glGetVertexAttribPointerv)>(&::glGetVertexAttribPointerv);
  gl_api.glHint = reinterpret_cast<decltype(gl_api.glHint)>(&::glHint);
  gl_api.glIsEnabled = reinterpret_cast<decltype(gl_api.glIsEnabled)>(&::glIsEnabled);
  gl_api.glIsFramebuffer = reinterpret_cast<decltype(gl_api.glIsFramebuffer)>(&::glIsFramebuffer);
  gl_api.glIsProgram = reinterpret_cast<decltype(gl_api.glIsProgram)>(&::glIsProgram);
  gl_api.glIsRenderbuffer = reinterpret_cast<decltype(gl_api.glIsRenderbuffer)>(&::glIsRenderbuffer);
  gl_api.glIsTexture = reinterpret_cast<decltype(gl_api.glIsTexture)>(&::glIsTexture);
  gl_api.glLineWidth = reinterpret_cast<decltype(gl_api.glLineWidth)>(&::glLineWidth);
  gl_api.glLinkProgram = reinterpret_cast<decltype(gl_api.glLinkProgram)>(&::glLinkProgram);
  gl_api.glPixelStorei = reinterpret_cast<decltype(gl_api.glPixelStorei)>(&::glPixelStorei);
  gl_api.glPolygonOffset = reinterpret_cast<decltype(gl_api.glPolygonOffset)>(&::glPolygonOffset);
  gl_api.glReadPixels = reinterpret_cast<decltype(gl_api.glReadPixels)>(&::glReadPixels);
  gl_api.glReleaseShaderCompiler = reinterpret_cast<decltype(gl_api.glReleaseShaderCompiler)>(&::glReleaseShaderCompiler);
  gl_api.glRenderbufferStorage = reinterpret_cast<decltype(gl_api.glRenderbufferStorage)>(&::glRenderbufferStorage);
  gl_api.glScissor = reinterpret_cast<decltype(gl_api.glScissor)>(&::glScissor);
  gl_api.glShaderBinary = reinterpret_cast<decltype(gl_api.glShaderBinary)>(&::glShaderBinary);
  gl_api.glShaderSource = reinterpret_cast<decltype(gl_api.glShaderSource)>(&::glShaderSource);
  gl_api.glStencilFunc = reinterpret_cast<decltype(gl_api.glStencilFunc)>(&::glStencilFunc);
  gl_api.glStencilFuncSeparate = reinterpret_cast<decltype(gl_api.glStencilFuncSeparate)>(&::glStencilFuncSeparate);
  gl_api.glStencilMask = reinterpret_cast<decltype(gl_api.glStencilMask)>(&::glStencilMask);
  gl_api.glStencilMaskSeparate = reinterpret_cast<decltype(gl_api.glStencilMaskSeparate)>(&::glStencilMaskSeparate);
  gl_api.glStencilOp = reinterpret_cast<decltype(gl_api.glStencilOp)>(&::glStencilOp);
  gl_api.glStencilOpSeparate = reinterpret_cast<decltype(gl_api.glStencilOpSeparate)>(&::glStencilOpSeparate);
  gl_api.glTexImage2D = reinterpret_cast<decltype(gl_api.glTexImage2D)>(&::glTexImage2D);
  gl_api.glTexParameterf = reinterpret_cast<decltype(gl_api.glTexParameterf)>(&::glTexParameterf);
  gl_api.glTexParameteri = reinterpret_cast<decltype(gl_api.glTexParameteri)>(&::glTexParameteri);
  gl_api.glTexParameteriv = reinterpret_cast<decltype(gl_api.glTexParameteriv)>(&::glTexParameteriv);
  gl_api.glTexSubImage2D = reinterpret_cast<decltype(gl_api.glTexSubImage2D)>(&::glTexSubImage2D);
  gl_api.glUniform1f = reinterpret_cast<decltype(gl_api.glUniform1f)>(&::glUniform1f);
  gl_api.glUniform1fv = reinterpret_cast<decltype(gl_api.glUniform1fv)>(&::glUniform1fv);
  gl_api.glUniform1i = reinterpret_cast<decltype(gl_api.glUniform1i)>(&::glUniform1i);
  gl_api.glUniform1iv = reinterpret_cast<decltype(gl_api.glUniform1iv)>(&::glUniform1iv);
  gl_api.glUniform2f = reinterpret_cast<decltype(gl_api.glUniform2f)>(&::glUniform2f);
  gl_api.glUniform2fv = reinterpret_cast<decltype(gl_api.glUniform2fv)>(&::glUniform2fv);
  gl_api.glUniform2i = reinterpret_cast<decltype(gl_api.glUniform2i)>(&::glUniform2i);
  gl_api.glUniform2iv = reinterpret_cast<decltype(gl_api.glUniform2iv)>(&::glUniform2iv);
  gl_api.glUniform3f = reinterpret_cast<decltype(gl_api.glUniform3f)>(&::glUniform3f);
  gl_api.glUniform3fv = reinterpret_cast<decltype(gl_api.glUniform3fv)>(&::glUniform3fv);
  gl_api.glUniform3i = reinterpret_cast<decltype(gl_api.glUniform3i)>(&::glUniform3i);
  gl_api.glUniform3iv = reinterpret_cast<decltype(gl_api.glUniform3iv)>(&::glUniform3iv);
  gl_api.glUniform4f = reinterpret_cast<decltype(gl_api.glUniform4f)>(&::glUniform4f);
  gl_api.glUniform4fv = reinterpret_cast<decltype(gl_api.glUniform4fv)>(&::glUniform4fv);
  gl_api.glUniform4i = reinterpret_cast<decltype(gl_api.glUniform4i)>(&::glUniform4i);
  gl_api.glUniform4iv = reinterpret_cast<decltype(gl_api.glUniform4iv)>(&::glUniform4iv);
  gl_api.glUniformMatrix2fv = reinterpret_cast<decltype(gl_api.glUniformMatrix2fv)>(&::glUniformMatrix2fv);
  gl_api.glUniformMatrix3fv = reinterpret_cast<decltype(gl_api.glUniformMatrix3fv)>(&::glUniformMatrix3fv);
  gl_api.glUniformMatrix4fv = reinterpret_cast<decltype(gl_api.glUniformMatrix4fv)>(&::glUniformMatrix4fv);
  gl_api.glUseProgram = reinterpret_cast<decltype(gl_api.glUseProgram)>(&::glUseProgram);
  gl_api.glVertexAttrib1f = reinterpret_cast<decltype(gl_api.glVertexAttrib1f)>(&::glVertexAttrib1f);
  gl_api.glVertexAttrib1fv = reinterpret_cast<decltype(gl_api.glVertexAttrib1fv)>(&::glVertexAttrib1fv);
  gl_api.glVertexAttrib2f = reinterpret_cast<decltype(gl_api.glVertexAttrib2f)>(&::glVertexAttrib2f);
  gl_api.glVertexAttrib2fv = reinterpret_cast<decltype(gl_api.glVertexAttrib2fv)>(&::glVertexAttrib2fv);
  gl_api.glVertexAttrib3f = reinterpret_cast<decltype(gl_api.glVertexAttrib3f)>(&::glVertexAttrib3f);
  gl_api.glVertexAttrib3fv = reinterpret_cast<decltype(gl_api.glVertexAttrib3fv)>(&::glVertexAttrib3fv);
  gl_api.glVertexAttrib4f = reinterpret_cast<decltype(gl_api.glVertexAttrib4f)>(&::glVertexAttrib4f);
  gl_api.glVertexAttrib4fv = reinterpret_cast<decltype(gl_api.glVertexAttrib4fv)>(&::glVertexAttrib4fv);
  gl_api.glVertexAttribPointer = reinterpret_cast<decltype(gl_api.glVertexAttribPointer)>(&::glVertexAttribPointer);
  gl_api.glViewport = reinterpret_cast<decltype(gl_api.glViewport)>(&::glViewport);
  gl_api.glReadBuffer = reinterpret_cast<decltype(gl_api.glReadBuffer)>(&::glReadBuffer);
  gl_api.glDrawRangeElements = reinterpret_cast<decltype(gl_api.glDrawRangeElements)>(&::glDrawRangeElements);
  gl_api.glGenQueries = reinterpret_cast<decltype(gl_api.glGenQueries)>(&::glGenQueries);
  gl_api.glDeleteQueries = reinterpret_cast<decltype(gl_api.glDeleteQueries)>(&::glDeleteQueries);
  gl_api.glBeginQuery = reinterpret_cast<decltype(gl_api.glBeginQuery)>(&::glBeginQuery);
  gl_api.glEndQuery = reinterpret_cast<decltype(gl_api.glEndQuery)>(&::glEndQuery);
  gl_api.glUnmapBuffer = reinterpret_cast<decltype(gl_api.glUnmapBuffer)>(&::glUnmapBuffer);
  gl_api.glBlitFramebuffer = reinterpret_cast<decltype(gl_api.glBlitFramebuffer)>(&::glBlitFramebuffer);
  gl_api.glMapBufferRange = reinterpret_cast<decltype(gl_api.glMapBufferRange)>(&::glMapBufferRange);
  gl_api.glFlushMappedBufferRange = reinterpret_cast<decltype(gl_api.glFlushMappedBufferRange)>(&::glFlushMappedBufferRange);
  gl_api.glBindVertexArray = reinterpret_cast<decltype(gl_api.glBindVertexArray)>(&::glBindVertexArray);
  gl_api.glDeleteVertexArrays = reinterpret_cast<decltype(gl_api.glDeleteVertexArrays)>(&::glDeleteVertexArrays);
  gl_api.glGenVertexArrays = reinterpret_cast<decltype(gl_api.glGenVertexArrays)>(&::glGenVertexArrays);
  gl_api.glBindBufferRange = reinterpret_cast<decltype(gl_api.glBindBufferRange)>(&::glBindBufferRange);
  gl_api.glBindBufferBase = reinterpret_cast<decltype(gl_api.glBindBufferBase)>(&::glBindBufferBase);
  gl_api.glGetStringi = reinterpret_cast<decltype(gl_api.glGetStringi)>(&::glGetStringi);
  gl_api.glGetUniformBlockIndex = reinterpret_cast<decltype(gl_api.glGetUniformBlockIndex)>(&::glGetUniformBlockIndex);
  gl_api.glUniformBlockBinding = reinterpret_cast<decltype(gl_api.glUniformBlockBinding)>(&::glUniformBlockBinding);
  gl_api.glDrawArraysInstanced = reinterpret_cast<decltype(gl_api.glDrawArraysInstanced)>(&::glDrawArraysInstanced);
  gl_api.glDrawElementsInstanced = reinterpret_cast<decltype(gl_api.glDrawElementsInstanced)>(&::glDrawElementsInstanced);
  gl_api.glGenSamplers = reinterpret_cast<decltype(gl_api.glGenSamplers)>(&::glGenSamplers);
  gl_api.glDeleteSamplers = reinterpret_cast<decltype(gl_api.glDeleteSamplers)>(&::glDeleteSamplers);
  gl_api.glBindSampler = reinterpret_cast<decltype(gl_api.glBindSampler)>(&::glBindSampler);
  gl_api.glSamplerParameteri = reinterpret_cast<decltype(gl_api.glSamplerParameteri)>(&::glSamplerParameteri);
  gl_api.glSamplerParameterf = reinterpret_cast<decltype(gl_api.glSamplerParameterf)>(&::glSamplerParameterf);
  gl_api.glVertexAttribDivisor = reinterpret_cast<decltype(gl_api.glVertexAttribDivisor)>(&::glVertexAttribDivisor);
  gl_api.glGetProgramBinary = reinterpret_cast<decltype(gl_api.glGetProgramBinary)>(&::glGetProgramBinary);
  gl_api.glProgramBinary = reinterpret_cast<decltype(gl_api.glProgramBinary)>(&::glProgramBinary);
}
} // namespace
extern "C" {
const SbEglInterface* SbGetEglInterface() {
  pthread_once(&once,set_interfaces);
  return &egl_api;
}
const SbGlesInterface* SbGetGlesInterface() {
  pthread_once(&once,set_interfaces);
  return &gl_api;
}
}
