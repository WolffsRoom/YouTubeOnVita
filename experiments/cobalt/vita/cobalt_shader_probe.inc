// Link probe for Cobalt's direct-GLES rasterizer rounded-corner shader, which
// hung glLinkProgram during the real Cobalt boot (2026-11-17). Variants are
// ordered from least to most suspicious; each logs before linking so that a
// hang identifies the construct responsible.
static const char kRCornerVS[] =
  "uniform vec4 u_clip_adjustment;\n"
  "uniform mat3 u_view_matrix;\n"
  "attribute vec2 a_position;\n"
  "attribute vec4 a_rcorner;\n"
  "attribute vec2 a_texcoord;\n"
  "varying vec4 v_rcorner;\n"
  "varying vec2 v_texcoord;\n"
  "void main() {\n"
  "  vec3 pos2d = u_view_matrix * vec3(a_position, 1);\n"
  "  gl_Position = vec4(pos2d.xy * u_clip_adjustment.xy +\n"
  "                     u_clip_adjustment.zw, 0, pos2d.z);\n"
  "  v_rcorner = a_rcorner;\n"
  "  v_texcoord = a_texcoord;\n"
  "}\n";

struct ShaderVariant { const char* name; const char* fs; };
static const ShaderVariant kRCornerVariants[] = {
  {"plain-no-pragma-no-highp-sqrt",
   "precision mediump float;\n"
   "uniform vec4 u_color;\n"
   "varying vec4 v_rcorner;\n"
   "varying vec2 v_texcoord;\n"
   "float IsOutsideRCorner(vec4 rcorner) {\n"
   "  vec2 scaled = max(rcorner.xy, 0.0);\n"
   "  float implicit = dot(scaled, scaled) - 1.0;\n"
   "  vec2 gradient = 2.0 * scaled * rcorner.zw;\n"
   "  float inv_gradient = 16.0 / sqrt(max(dot(gradient, gradient), 0.0001));\n"
   "  return clamp(0.5 + implicit * inv_gradient, 0.0, 1.0);\n"
   "}\n"
   "uniform vec4 u_texcoord_clamp_rgba;\n"
   "uniform sampler2D u_texture_rgba;\n"
   "void main() {\n"
   "  float scale = IsOutsideRCorner(v_rcorner);\n"
   "  gl_FragColor = u_color * (1.0 - scale) * texture2D(u_texture_rgba,\n"
   "      clamp(v_texcoord, u_texcoord_clamp_rgba.xy, u_texcoord_clamp_rgba.zw));\n"
   "}\n"},
  {"inversesqrt",
   "precision mediump float;\n"
   "uniform vec4 u_color;\n"
   "varying vec4 v_rcorner;\n"
   "varying vec2 v_texcoord;\n"
   "float IsOutsideRCorner(vec4 rcorner) {\n"
   "  vec2 scaled = max(rcorner.xy, 0.0);\n"
   "  float implicit = dot(scaled, scaled) - 1.0;\n"
   "  vec2 gradient = 2.0 * scaled * rcorner.zw;\n"
   "  float inv_gradient = 16.0 * inversesqrt(max(dot(gradient, gradient), 0.0001));\n"
   "  return clamp(0.5 + implicit * inv_gradient, 0.0, 1.0);\n"
   "}\n"
   "uniform vec4 u_texcoord_clamp_rgba;\n"
   "uniform sampler2D u_texture_rgba;\n"
   "void main() {\n"
   "  float scale = IsOutsideRCorner(v_rcorner);\n"
   "  gl_FragColor = u_color * (1.0 - scale) * texture2D(u_texture_rgba,\n"
   "      clamp(v_texcoord, u_texcoord_clamp_rgba.xy, u_texcoord_clamp_rgba.zw));\n"
   "}\n"},
  {"highp-locals-and-const",
   "precision mediump float;\n"
   "uniform vec4 u_color;\n"
   "varying vec4 v_rcorner;\n"
   "varying vec2 v_texcoord;\n"
   "float IsOutsideRCorner(vec4 rcorner) {\n"
   "  highp vec2 scaled = max(rcorner.xy, 0.0);\n"
   "  highp float implicit = dot(scaled, scaled) - 1.0;\n"
   "  const highp float kRCornerGradientScale = 16.0;\n"
   "  highp vec2 gradient = 2.0 * scaled * rcorner.zw;\n"
   "  highp float inv_gradient = kRCornerGradientScale *\n"
   "      inversesqrt(max(dot(gradient, gradient), 0.0001));\n"
   "  return clamp(0.5 + implicit * inv_gradient, 0.0, 1.0);\n"
   "}\n"
   "uniform vec4 u_texcoord_clamp_rgba;\n"
   "uniform sampler2D u_texture_rgba;\n"
   "vec4 GetRgba() {\n"
   "  return texture2D(u_texture_rgba,\n"
   "      clamp(v_texcoord, u_texcoord_clamp_rgba.xy, u_texcoord_clamp_rgba.zw));\n"
   "}\n"
   "void main() {\n"
   "  float scale = IsOutsideRCorner(v_rcorner);\n"
   "  gl_FragColor = u_color * (1.0 - scale) * GetRgba();\n"
   "}\n"},
  {"exact-cobalt-with-pragma-array",
   "precision mediump float;\n"
   "uniform vec4 u_color;\n"
   "varying vec4 v_rcorner;\n"
   "varying vec2 v_texcoord;\n"
   "float IsOutsideRCorner(vec4 rcorner) {\n"
   "  highp vec2 scaled = max(rcorner.xy, 0.0);\n"
   "  highp float implicit = dot(scaled, scaled) - 1.0;\n"
   "  const highp float kRCornerGradientScale = 16.0;\n"
   "  highp vec2 gradient = 2.0 * scaled * rcorner.zw;\n"
   "  highp float inv_gradient = kRCornerGradientScale *\n"
   "      inversesqrt(max(dot(gradient, gradient), 0.0001));\n"
   "  return clamp(0.5 + implicit * inv_gradient, 0.0, 1.0);\n"
   "}\n"
   "uniform vec4 u_texcoord_clamp_rgba;\n"
   "uniform sampler2D u_texture_rgba;\n"
   "#pragma array u_texcoord_clamp(u_texcoord_clamp_rgba);\n"
   "#pragma array u_texture(u_texture_rgba);\n"
   "vec4 GetRgba() {\n"
   "  return texture2D(u_texture_rgba,\n"
   "      clamp(v_texcoord, u_texcoord_clamp_rgba.xy, u_texcoord_clamp_rgba.zw));\n"
   "}\n"
   "void main() {\n"
   "  float scale = IsOutsideRCorner(v_rcorner);\n"
   "  gl_FragColor = u_color * (1.0 - scale) * GetRgba();\n"
   "}\n"},
};

static void probe_cobalt_rcorner_shaders(const SbGlesInterface* gl) {
  for (const ShaderVariant& v : kRCornerVariants) {
    char line[160];
    GLuint vs = gl->glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = gl->glCreateShader(GL_FRAGMENT_SHADER);
    const char* vsrc = kRCornerVS;
    const char* fsrc = v.fs;
    gl->glShaderSource(vs, 1, &vsrc, nullptr);
    gl->glShaderSource(fs, 1, &fsrc, nullptr);
    gl->glCompileShader(vs);
    gl->glCompileShader(fs);
    GLuint prog = gl->glCreateProgram();
    gl->glAttachShader(prog, vs);
    gl->glAttachShader(prog, fs);
    gl->glBindAttribLocation(prog, 0, "a_position");
    gl->glBindAttribLocation(prog, 1, "a_rcorner");
    gl->glBindAttribLocation(prog, 2, "a_texcoord");
    snprintf(line, sizeof(line), "RCORNER_LINK_BEGIN %s", v.name);
    out(line);
    gl->glLinkProgram(prog);
    GLint linked = 0;
    gl->glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    snprintf(line, sizeof(line), "Cobalt rounded-corner shader links: %s", v.name);
    check(linked == GL_TRUE, line);
    gl->glDeleteProgram(prog);
    gl->glDeleteShader(vs);
    gl->glDeleteShader(fs);
  }
}
