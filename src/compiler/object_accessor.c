#include "compiler/object_accessor.h"
#include "compiler/function_argument.h"
#include "compiler/function_body.h"
#include "compiler/node.h"
#include "compiler/object_key.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>
static void neo_ast_object_accessor_dispose(neo_allocator_t allocator,
                                            neo_ast_object_accessor_t node) {
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->name);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_object_accessor(neo_allocator_t allocator,
                                  neo_ast_object_accessor_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_OBJECT_ACCESSOR"));
  neo_variable_set(variable, "arguments",
                   neo_ast_node_list_serialize(allocator, node->arguments));
  neo_variable_set(variable, "name",
                   neo_ast_node_serialize(allocator, node->name));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  neo_variable_set(
      variable, "kind",
      neo_create_variable_string(allocator, node->kind == NEO_ACCESSOR_KIND_GET
                                                ? "NEO_ACCESSOR_KIND_GET"
                                                : "NEO_ACCESSOR_KIND_SET"));
  neo_variable_set(variable, "computed",
                   neo_create_variable_boolean(allocator, node->computed));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_object_accessor_t
neo_create_ast_object_accessor(neo_allocator_t allocator) {
  neo_ast_object_accessor_t node =
      neo_allocator_alloc2(allocator, neo_ast_object_accessor);
  node->node.type = NEO_NODE_TYPE_OBJECT_ACCESSOR;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_object_accessor;
  node->body = NULL;
  node->name = NULL;
  node->kind = NEO_ACCESSOR_KIND_GET;
  node->computed = false;
  neo_list_initialize_t initialize = {true};
  node->arguments = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_object_accessor(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_object_accessor_t node = neo_create_ast_object_accessor(allocator);
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || (!neo_location_is(token->location, "get") &&
                 !neo_location_is(token->location, "set"))) {
    goto onerror;
  }
  if (neo_location_is(token->location, "get")) {
    node->kind = NEO_ACCESSOR_KIND_GET;
  } else {
    node->kind = NEO_ACCESSOR_KIND_SET;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->name = TRY(neo_ast_read_object_key(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->name) {
    node->name =
        TRY(neo_ast_read_object_computed_key(allocator, file, &current)) {
      goto onerror;
    }
    node->computed = true;
  }
  if (!node->name) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '(') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    for (;;) {
      neo_ast_node_t argument =
          TRY(neo_ast_read_function_argument(allocator, file, &current)) {
        goto onerror;
      }
      if (!argument) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->arguments, argument);
      neo_compile_scope_declar(allocator, neo_complile_scope_get_current(),
                               argument, NEO_COMPILE_VARIABLE_CONST);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ')') {
        break;
      }
      if (*current.offset != ',') {
        goto onerror;
      }
      if (((neo_ast_function_argument_t)argument)->identifier->type ==
          NEO_NODE_TYPE_PATTERN_REST) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    }
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->body = TRY(neo_ast_read_function_body(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->body) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  node->node.scope = neo_compile_scope_pop(scope);
  *position = current;
  return &node->node;
onerror:
  if (scope && !node->node.scope) {
    scope = neo_compile_scope_pop(scope);
    neo_allocator_free(allocator, scope);
  }
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}