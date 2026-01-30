/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#pragma once

#define KSEARCHTREE_ROOT(type, nodetype) \
    struct type {                        \
        struct nodetype *st_root;        \
    }

#define KSEARCHTREE_INIT(root) (root)->st_root = NULL

#define KSEARCHTREE_NODE(nodetype, weighttype) \
    struct {                                   \
        struct nodetype *stn_parent;           \
        struct nodetype *stn_left;             \
        struct nodetype *stn_right;            \
        weighttype       stn_weight;           \
    }

#define KSEARCHTREE_INSERT_AT(rootnode, newelem, weight, field)             \
    {                                                                       \
        typeof(rootnode)  _st_root   = rootnode;                            \
        typeof(rootnode) *_st_ptr    = &(rootnode);                         \
        typeof(rootnode)  _st_parent = NULL;                                \
        (newelem)->field.stn_weight  = weight;                              \
        while (NULL != _st_root) {                                          \
            _st_parent = _st_root;                                          \
            if ((newelem)->field.stn_weight < _st_root->field.stn_weight) { \
                _st_ptr = &_st_root->field.stn_left;                        \
            } else {                                                        \
                _st_ptr = &_st_root->field.stn_right;                       \
            }                                                               \
            _st_root = *_st_ptr;                                            \
        }                                                                   \
        *_st_ptr                    = newelem;                              \
        (newelem)->field.stn_parent = _st_parent;                           \
        (newelem)->field.stn_left   = NULL;                                 \
        (newelem)->field.stn_right  = NULL;                                 \
    }

#define KSEARCHTREE_INSERT(root, newelem, weight, field) \
    KSEARCHTREE_INSERT_AT((root)->st_root, newelem, weight, field)

#define KSEARCHTREE_REMOVE(root, elem, field)                                 \
    {                                                                         \
        if (NULL != (elem)->field.stn_parent) {                               \
            if ((elem) == (elem)->field.stn_parent->field.stn_left) {         \
                (elem)->field.stn_parent->field.stn_left = NULL;              \
            } else if ((elem) == (elem)->field.stn_parent->field.stn_right) { \
                (elem)->field.stn_parent->field.stn_right = NULL;             \
            } else {                                                          \
                KASSERT(!"KSEARCHTREE_REMOVE called on orphan node");         \
            }                                                                 \
        }                                                                     \
        (elem)->field.stn_parent = NULL;                                      \
    }

#define KSEARCHTREE_ROOT_NODE(root) ((root)->st_root)

#define KSEARCHTREE_PARENT(node, field) ((node)->field.stn_parent)
#define KSEARCHTREE_LEFT(node, field)   ((node)->field.stn_left)
#define KSEARCHTREE_RIGHT(node, field)  ((node)->field.stn_right)
