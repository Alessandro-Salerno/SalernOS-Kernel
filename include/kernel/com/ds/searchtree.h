#pragma once

#define SEARCHTREE_ROOT(type, nodetype) \
    struct type {                       \
        struct nodetype *st_root;       \
    }

#define SEARCHTREE_INIT(root) (root)->st_root = NULL

#define SEARCHTREE_NODE(nodetype, weighttype) \
    struct {                                  \
        struct nodetype *stn_parent;          \
        struct nodetype *stn_left;            \
        struct nodetype *stn_right;           \
        weighttype stn_weight;                \
    }

#define SEARCHTREE_INSERT_AT(rootnode, newelem, weight, field)              \
    {                                                                       \
        typeof(rootnode) _st_root = rootnode;                               \
        typeof(rootnode) *_st_ptr = &(rootnode);                            \
        typeof(rootnode) _st_parent = NULL;                                 \
        (newelem)->field.stn_weight = weight;                               \
        while (_st_root != NULL) {                                          \
            _st_parent = _st_root;                                          \
            if ((newelem)->field.stn_weight < _st_root->field.stn_weight) { \
                _st_ptr = &_st_root->field.stn_left;                        \
            } else {                                                        \
                _st_ptr = &_st_root->field.stn_right;                       \
            }                                                               \
            _st_root = *_st_ptr;                                            \
        }                                                                   \
        *_st_ptr = newelem;                                                 \
        (newelem)->field.stn_parent = _st_parent;                           \
        (newelem)->field.stn_left = NULL;                                   \
        (newelem)->field.stn_right = NULL;                                  \
    }

#define SEARCHTREE_INSERT(root, newelem, weight, field) \
    SEARCHTREE_INSERT_AT((root)->st_root, newelem, weight, field)

#define SEARCHTREE_REMOVE(root, elem, field)                                  \
    {                                                                         \
        if ((elem)->field.stn_parent != NULL) {                               \
            if ((elem) == (elem)->field.stn_parent->field.stn_left) {         \
                (elem)->field.stn_parent->field.stn_left = NULL;              \
            } else if ((elem) == (elem)->field.stn_parent->field.stn_right) { \
                (elem)->field.stn_parent->field.stn_right = NULL;             \
            } else {                                                          \
                assert(!"SEARCHTREE_REMOVE called on orphan node");           \
            }                                                                 \
        }                                                                     \
        (elem)->field.stn_parent = NULL;                                      \
    }

#define SEARCHTREE_ROOT_NODE(root) ((root)->st_root)

#define SEARCHTREE_PARENT(node, field) ((node)->field.stn_parent)
#define SEARCHTREE_LEFT(node, field)   ((node)->field.stn_left)
#define SEARCHTREE_RIGHT(node, field)  ((node)->field.stn_right)
