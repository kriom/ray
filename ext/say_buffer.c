#include "say.h"

static bool say_has_vao() {
  return GLEW_ARB_vertex_array_object ||
    GLEW_VERSION_3_0;
}

static GLuint say_current_vbo = 0;
static say_context *say_vbo_last_context = NULL;

static void say_vbo_make_current(GLuint vbo) {
  say_context *context = say_context_current();

  if (context != say_vbo_last_context ||
      vbo != say_current_vbo) {
    say_current_vbo      = vbo;
    say_vbo_last_context = context;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
  }
}

static GLuint       say_current_vao      = 0;
static say_context *say_vao_last_context = NULL;

typedef struct {
  GLuint vao;
  say_context *context;
} say_vao_pair;

static void say_vao_make_current(GLuint vao) {
  say_context *context = say_context_current();

  if (context != say_vao_last_context ||
      vao != say_current_vao) {
    say_current_vao      = vao;
    say_vao_last_context = context;

    glBindVertexArray(vao);

    say_index_buffer_rebind();
  }
}

static say_buffer  *say_current_buffer      = NULL;
static say_context *say_buffer_last_context = NULL;

static void say_buffer_setup_pointer(say_buffer *buf);

static void say_buffer_make_current(say_buffer *buf) {
  say_context *context = say_context_current();

  if (context != say_buffer_last_context ||
      buf != say_current_buffer) {
    say_current_buffer      = buf;
    say_buffer_last_context = context;

    say_buffer_setup_pointer(buf);
  }
}

static void say_vbo_will_delete(GLuint vbo) {
  if (vbo == say_current_vbo)
    say_current_vbo = 0;
}

static void say_buffer_will_delete(say_buffer *buf) {
  if (buf == say_current_buffer)
    say_current_buffer = NULL;
}

static void say_buffer_delete_vao_pair(say_vao_pair *pair) {
  /* TODO: finding out if the context is still alive, to avoid leaks */
  if (say_vao_last_context == pair->context && say_current_vao == pair->vao) {
    say_current_vao = 0;
    glDeleteVertexArrays(1, &pair->vao);

    say_index_buffer_rebind();
  }

  free(pair);
}

static size_t say_buffer_register_pointer(size_t attr_i, say_vertex_elem_type t,
                                          size_t stride, size_t offset) {
  switch (t) {
  case SAY_FLOAT:
    glVertexAttribPointer(attr_i, 1, GL_FLOAT, GL_FALSE, stride,
                          (void*)offset);
    return offset + sizeof(GLfloat);
  case SAY_INT:
    glVertexAttribPointer(attr_i, 1, GL_INT, GL_FALSE, stride,
                          (void*)offset);
    return offset + sizeof(GLint);
  case SAY_UBYTE:
    glVertexAttribPointer(attr_i, 1, GL_UNSIGNED_BYTE, GL_FALSE, stride,
                          (void*)offset);
    return offset + sizeof(GLubyte);
  case SAY_BOOL:
    glVertexAttribPointer(attr_i, 1, GL_INT, GL_FALSE, stride,
                          (void*)offset);
    return offset + sizeof(GLint);

  case SAY_COLOR:
    glVertexAttribPointer(attr_i, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          (void*)offset);
    return offset + sizeof(GLubyte) * 4;
  case SAY_VECTOR2:
    glVertexAttribPointer(attr_i, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offset);
    return offset + sizeof(GLfloat) * 2;
  case SAY_VECTOR3:
    glVertexAttribPointer(attr_i, 3, GL_FLOAT, GL_FALSE, stride,
                          (void*)offset);
    return offset + sizeof(GLfloat) * 3;
  }

  return offset;
}

static void say_buffer_setup_pointer(say_buffer *buf) {
  say_vbo_make_current(buf->vbo);

  say_vertex_type *type = say_get_vertex_type(buf->vtype);

  size_t count = say_vertex_type_get_elem_count(type);
  if (count == 0) /* No attrbutes to set */
    return;

  size_t stride = say_vertex_type_get_size(type);
  size_t offset = 0;

  size_t instance_stride = say_vertex_type_get_instance_size(type);
  size_t instance_offset = 0;

  /*
   * This fixes a bug on OSX (with OpenGL 2.1). Nothing is drawn unless vertex
   * attribute 0 is enabled.
   *
   * We set its data to the same as those used by the first element to ensure
   * memory we do not own isn't accessed.
   */
  say_vertex_elem_type t = say_vertex_type_get_type(type, 0);

  if (say_vertex_type_is_per_instance(type, 0)) {
    say_vbo_make_current(buf->instance_vbo);
    say_buffer_register_pointer(0, t, instance_stride, instance_offset);
  }
  else {
    say_vbo_make_current(buf->vbo);
    say_buffer_register_pointer(0, t, stride, offset);
  }

  glEnableVertexAttribArray(0);

  size_t i = 0;
  for (; i < count; i++) {
    t = say_vertex_type_get_type(type, i);

    if (say_vertex_type_is_per_instance(type, i)) {
      say_vbo_make_current(buf->instance_vbo);
      instance_offset = say_buffer_register_pointer(i + 1, t, instance_stride,
                                                    instance_offset);
    }
    else {
      say_vbo_make_current(buf->vbo);
      offset = say_buffer_register_pointer(i + 1, t, stride, offset);
    }

    glEnableVertexAttribArray(i + 1);
    if (glVertexAttribDivisor) {
      if (say_vertex_type_is_per_instance(type, i))
        glVertexAttribDivisor(i + 1, 1);
      else
        glVertexAttribDivisor(i + 1, 0);
    }
  }

  /*
   * Say will always use all the attribs. Disable all of them until
   * finding one that is already disabled.
   */
  for (; i < GL_MAX_VERTEX_ATTRIBS - 1; i++) {
    GLint enabled;
    glGetVertexAttribiv(i + 1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);

    if (enabled)
      glDisableVertexAttribArray(i + 1);
    else
      break;
  }
}

static void say_buffer_build_vao(say_buffer *buf, GLuint vao) {
  say_vao_make_current(vao);
  say_buffer_setup_pointer(buf);
}

static GLuint say_buffer_get_vao(say_buffer *buf) {
  say_context *ctxt = say_context_current();
  uint32_t count = ctxt->count;

  say_vao_pair *pair = say_table_get(buf->vaos, count);

  if (!pair) {
    pair = malloc(sizeof(say_vao_pair));
    say_table_set(buf->vaos, count, pair);

    pair->context = ctxt;

    glGenVertexArrays(1, &pair->vao);
    say_buffer_build_vao(buf, pair->vao);

    return pair->vao;
  }
  else {
    return pair->vao;
  }
}

say_buffer *say_buffer_create(size_t vtype, GLenum type, size_t size) {
  say_context_ensure();

  say_buffer *buf = (say_buffer*)malloc(sizeof(say_buffer));

  if (say_has_vao())
    buf->vaos = say_table_create((say_destructor)say_buffer_delete_vao_pair);
  else
    buf->vaos = NULL;

  buf->vtype = vtype;

  glGenBuffers(1, &buf->vbo);
  say_vbo_make_current(buf->vbo);

  buf->type = type;

  say_vertex_type *vtype_ref = say_get_vertex_type(vtype);
  size_t byte_size = say_vertex_type_get_size(vtype_ref);
  mo_array_init(&buf->buffer, byte_size);
  mo_array_resize(&buf->buffer, size);

  glBufferData(GL_ARRAY_BUFFER, size * byte_size, NULL, type);

  buf->instance_vbo    = 0;
  buf->instance_buffer = NULL;

  if (say_vertex_type_has_instance_data(vtype_ref)) {
    glGenBuffers(1, &buf->instance_vbo);
    say_vbo_make_current(buf->instance_vbo);

    byte_size = say_vertex_type_get_instance_size(vtype_ref);
    buf->instance_buffer = say_array_create(byte_size, NULL, NULL);
  }

  return buf;
}

void say_buffer_free(say_buffer *buf) {
  say_context_ensure();

  if (buf->vaos)
    say_table_free(buf->vaos);
  else
    say_buffer_will_delete(buf);

  say_vbo_will_delete(buf->vbo);
  glDeleteBuffers(1, &buf->vbo);

  if (buf->instance_vbo) {
    say_vbo_will_delete(buf->instance_vbo);
    glDeleteBuffers(1, &buf->instance_vbo);

    say_array_free(buf->instance_buffer);
  }

  mo_array_release(&buf->buffer);
  free(buf);
}

bool say_buffer_has_instance(say_buffer *buf) {
  return buf->instance_buffer != NULL;
}

void *say_buffer_get_vertex(say_buffer *buf, size_t id) {
  return mo_array_at(&buf->buffer, id);
}

void *say_buffer_get_instance(say_buffer *buf, size_t id) {
  return say_array_get(buf->instance_buffer, id);
}

void say_buffer_bind(say_buffer *buf) {
  say_context_ensure();

  if (say_has_vao())
    say_vao_make_current(say_buffer_get_vao(buf));
  else
    say_buffer_make_current(buf);
}

void say_buffer_bind_vbo(say_buffer *buf) {
  say_context_ensure();
  say_vbo_make_current(buf->vbo);
}

void say_buffer_bind_instance_vbo(say_buffer *buf) {
  say_context_ensure();
  say_vbo_make_current(buf->instance_vbo);
}

void say_buffer_unbind() {
  say_context_ensure();

  if (say_has_vao())
    say_vao_make_current(0);
  else { /* disable vertex attribs */
    for (size_t i = 1; i < GL_MAX_VERTEX_ATTRIBS; i++) {
      GLint enabled;
      glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);

      if (enabled)
        glDisableVertexAttribArray(i);
      else
        break;
    }
  }
}

void say_buffer_unbind_vbo() {
  say_context_ensure();
  say_vbo_make_current(0);
}

void say_buffer_update_part(say_buffer *buf, size_t id, size_t size) {
  if (size == 0) return;

  say_context_ensure();

  size_t byte_size = buf->buffer.el_size;
  say_vbo_make_current(buf->vbo);
  glBufferSubData(GL_ARRAY_BUFFER,
                  byte_size * id,
                  byte_size * size,
                  say_buffer_get_vertex(buf, id));
}

void say_buffer_update(say_buffer *buf) {
  say_buffer_update_part(buf, 0, buf->buffer.size);
}

size_t say_buffer_get_size(say_buffer *buf) {
  return buf->buffer.size;
}

void say_buffer_resize(say_buffer *buf, size_t size) {
  mo_array_resize(&buf->buffer, size);

  say_context_ensure();
  say_vbo_make_current(buf->vbo);
  glBufferData(GL_ARRAY_BUFFER,
               size * buf->buffer.size,
               say_buffer_get_vertex(buf, 0),
               buf->type);
}

void say_buffer_update_instance_part(say_buffer *buf, size_t id,
                                     size_t size) {
  if (size == 0) return;

  say_context_ensure();

  size_t byte_size = say_array_get_elem_size(buf->instance_buffer);
  say_vbo_make_current(buf->instance_vbo);
  glBufferSubData(GL_ARRAY_BUFFER,
                  byte_size * id,
                  byte_size * size,
                  say_buffer_get_instance(buf, id));
}

void say_buffer_update_instance(say_buffer *buf) {
  say_buffer_update_instance_part(buf, 0,
                                  say_array_get_size(buf->instance_buffer));
}

size_t say_buffer_get_instance_size(say_buffer *buf) {
  return say_array_get_size(buf->instance_buffer);
}

void say_buffer_resize_instance(say_buffer *buf, size_t size) {
  say_array_resize(buf->instance_buffer, size);

  say_context_ensure();
  say_vbo_make_current(buf->instance_vbo);
  glBufferData(GL_ARRAY_BUFFER,
               size * say_array_get_elem_size(buf->instance_buffer),
               say_buffer_get_instance(buf, 0),
               buf->type);
}
