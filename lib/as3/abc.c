/* abc.c

   Routines for handling Flash2 AVM2 ABC Actionscript

   Extension module for the rfxswf library.
   Part of the swftools package.

   Copyright (c) 2008 Matthias Kramm <kramm@quiss.org>
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <stdarg.h>
#include <assert.h>
#include "../rfxswf.h"
#include "../q.h"
#include "abc.h"

char stringbuffer[2048];

int abc_RegisterNameSpace(abc_file_t*file, const char*name);
int abc_RegisterPackageNameSpace(abc_file_t*file, const char*name);
int abc_RegisterPackageInternalNameSpace(abc_file_t*file, const char*name);
int abc_RegisterProtectedNameSpace(abc_file_t*file, const char*name);
int abc_RegisterExplicitNameSpace(abc_file_t*file, const char*name);
int abc_RegisterStaticProtectedNameSpace(abc_file_t*file, const char*name);
int abc_RegisterPrivateNameSpace(abc_file_t*file, const char*name);

/* TODO: switch to a datastructure with just values */
#define NO_KEY ""

static char* params_to_string(multiname_list_t*list)
{
    multiname_list_t*l;
    int n = list_length(list);
    char**names = (char**)malloc(sizeof(char*)*n);
    
    l = list;
    n = 0;
    int size = 0;
    while(l) {
        names[n] = multiname_to_string(l->multiname);
        size += strlen(names[n]) + 2;
        n++;l=l->next;
    }

    char* params = malloc(size+15);
    params[0]='(';
    params[1]=0;
    l = list;
    int s=0;
    n = 0;
    while(l) {
        if(s)
            strcat(params, ", ");
        strcat(params, names[n]);
        free(names[n]);
        l = l->next;
        n++;
        s=1;
    }
    free(names);
    /*char num[20];
    sprintf(num, "[%d params]", n);
    strcat(params, num);*/
    strcat(params, ")");
    int t;
    return params;
}

//#define DEBUG
#define DEBUG if(0)

static void parse_metadata(TAG*tag, abc_file_t*file, pool_t*pool)
{
    int t;
    int num_metadata = swf_GetU30(tag);

    DEBUG printf("%d metadata\n");
    for(t=0;t<num_metadata;t++) {
        const char*entry_name = pool_lookup_string(pool, swf_GetU30(tag));
        int num = swf_GetU30(tag);
        int s;
        DEBUG printf("  %s\n", entry_name);
        array_t*items = array_new();
        for(s=0;s<num;s++) {
            int i1 = swf_GetU30(tag);
            int i2 = swf_GetU30(tag);
            char*key = i1?pool_lookup_string(pool, i1):"";
            char*value = i2?pool_lookup_string(pool, i2):"";
            DEBUG printf("    %s=%s\n", key, value);
            array_append(items, key, strdup(value));
        }
        array_append(file->metadata, entry_name, items);
    }
}

void swf_CopyData(TAG*to, TAG*from, int len)
{
    unsigned char*data = malloc(len);
    swf_GetBlock(from, data, len);
    swf_SetBlock(to, data, len);
    free(data);
}

abc_file_t*abc_file_new()
{
    abc_file_t*f = malloc(sizeof(abc_file_t));
    memset(f, 0, sizeof(abc_file_t));
    f->metadata = array_new();

    f->methods = array_new();
    f->classes = array_new();
    f->scripts = array_new();
    f->method_bodies = array_new();

    return f;
}

#define CLASS_SEALED 1
#define CLASS_FINAL 2
#define CLASS_INTERFACE 4
#define CLASS_PROTECTED_NS 8

abc_class_t* abc_class_new(abc_file_t*file, multiname_t*classname, multiname_t*superclass) {
    
    NEW(abc_class_t,c);
    array_append(file->classes, NO_KEY, c);

    c->file = file;
    c->classname = classname;
    c->superclass = superclass;
    c->flags = 0;
    c->constructor = 0;
    c->static_constructor = 0;
    c->traits = list_new();
    return c;
}
abc_class_t* abc_class_new2(abc_file_t*pool, char*classname, char*superclass) 
{
    return abc_class_new(pool, multiname_fromstring(classname), multiname_fromstring(superclass));
}

void abc_class_sealed(abc_class_t*c)
{
    c->flags |= CLASS_SEALED;
}
void abc_class_final(abc_class_t*c)
{
    c->flags |= CLASS_FINAL;
}
void abc_class_interface(abc_class_t*c)
{
    c->flags |= CLASS_INTERFACE;
}
void abc_class_protectedNS(abc_class_t*c, char*namespace)
{
    c->protectedNS = namespace_new_protected(namespace);
    c->flags |= CLASS_PROTECTED_NS;
}
void abc_class_add_interface(abc_class_t*c, multiname_t*interface)
{
    list_append(c->interfaces, interface);
}

abc_method_body_t* add_method(abc_file_t*file, abc_class_t*cls, char*returntype, int num_params, va_list va)
{
    /* construct code (method body) object */
    NEW(abc_method_body_t,c);
    array_append(file->method_bodies, NO_KEY, c);
    c->file = file;
    c->traits = list_new();
    c->code = 0;

    /* construct method object */
    NEW(abc_method_t,m);
    array_append(file->methods, NO_KEY, m);

    if(returntype && strcmp(returntype, "void")) {
	m->return_type = multiname_fromstring(returntype);
    } else {
	m->return_type = 0;
    }
    int t;
    for(t=0;t<num_params;t++) {
	const char*param = va_arg(va, const char*);
	list_append(m->parameters, multiname_fromstring(param));
    }

    /* crosslink the two objects */
    m->body = c;
    c->method = m;

    return c;
}

abc_method_body_t* abc_class_constructor(abc_class_t*cls, char*returntype, int num_params, ...) 
{
    va_list va;
    va_start(va, num_params);
    abc_method_body_t* c = add_method(cls->file, cls, returntype, num_params, va);
    va_end(va);
    cls->constructor = c->method;
    return c;
}

abc_method_body_t* abc_class_staticconstructor(abc_class_t*cls, char*returntype, int num_params, ...) 
{
    va_list va;
    va_start(va, num_params);
    abc_method_body_t* c = add_method(cls->file, cls, returntype, num_params, va);
    va_end(va);
    cls->static_constructor = c->method;
    return c;
}

trait_t*trait_new(int type, multiname_t*name, int data1, int data2, constant_t*v)
{
    trait_t*trait = malloc(sizeof(trait_t));
    memset(trait, 0, sizeof(trait_t));
    trait->kind = type&0x0f;
    trait->attributes = type&0xf0;
    trait->name = name;
    trait->data1 = data1;
    trait->data2 = data2;
    trait->value = v;
    return trait;
}
trait_t*trait_new_member(multiname_t*type, multiname_t*name,constant_t*v)
{
    int kind = TRAIT_SLOT;
    trait_t*trait = malloc(sizeof(trait_t));
    memset(trait, 0, sizeof(trait_t));
    trait->kind = kind&0x0f;
    trait->attributes = kind&0xf0;
    trait->name = name;
    trait->type_name = type;
    return trait;
}
trait_t*trait_new_method(multiname_t*name, abc_method_t*m)
{
    int type = TRAIT_METHOD;
    trait_t*trait = malloc(sizeof(trait_t));
    memset(trait, 0, sizeof(trait_t));
    trait->kind = type&0x0f;
    trait->attributes = type&0xf0;
    trait->name = name;
    trait->method = m;
    return trait;
}

abc_method_body_t* abc_class_method(abc_class_t*cls, char*returntype, char*name, int num_params, ...)
{
    abc_file_t*file = cls->file;
    va_list va;
    va_start(va, num_params);
    abc_method_body_t* c = add_method(cls->file, cls, returntype, num_params, va);
    va_end(va);
    list_append(cls->traits, trait_new_method(multiname_fromstring(name), c->method));
    return c;
}

void abc_AddSlot(abc_class_t*cls, char*name, int slot, char*type)
{
    abc_file_t*file = cls->file;
    multiname_t*m_name = multiname_fromstring(name);
    multiname_t*m_type = multiname_fromstring(type);
    trait_t*t = trait_new_member(m_type, m_name, 0);
    t->slot_id = list_length(cls->traits);
    list_append(cls->traits, t);
}

void abc_method_body_addClassTrait(abc_method_body_t*code, char*multiname, int slotid, abc_class_t*cls)
{
    abc_file_t*file = code->file;
    multiname_t*m = multiname_fromstring(multiname);
    trait_t*trait = trait_new(TRAIT_CLASS, m, slotid, 0, 0);
    trait->cls = cls;
    list_append(code->traits, trait);
}

/* notice: traits of a method (body) belonging to an init script
   and traits of the init script are *not* the same thing */
int abc_initscript_addClassTrait(abc_script_t*script, multiname_t*multiname, abc_class_t*cls)
{
    abc_file_t*file = script->file;
    multiname_t*m = multiname_clone(multiname);
    int slotid = list_length(script->traits)+1;
    trait_t*trait = trait_new(TRAIT_CLASS, m, slotid, 0, 0);
    trait->cls = cls;
    list_append(script->traits, trait);
    return slotid;
}

abc_script_t* abc_initscript(abc_file_t*file, char*returntype, int num_params, ...) 
{
    va_list va;
    va_start(va, num_params);
    abc_method_body_t* c = add_method(file, 0, returntype, num_params, va);
    abc_script_t* s = malloc(sizeof(abc_script_t));
    s->method = c->method;
    s->traits = list_new();
    s->file = file;
    array_append(file->scripts, NO_KEY, s);
    va_end(va);
    return s;
}

static void traits_dump(FILE*fo, const char*prefix, trait_list_t*traits, abc_file_t*file);

static void dump_method(FILE*fo, const char*prefix, const char*type, const char*name, abc_method_t*m, abc_file_t*file)
{
    char*return_type = 0;
    if(m->return_type)
        return_type = multiname_to_string(m->return_type);
    else
        return_type = strdup("void");
    char*paramstr = params_to_string(m->parameters);
    fprintf(fo, "%s%s %s %s=%s %s (%d params)\n", prefix, type, return_type, name, m->name, paramstr, list_length(m->parameters));
    free(paramstr);paramstr=0;
    free(return_type);return_type=0;

    abc_method_body_t*c = m->body;
    if(!c) {
        return;
    }
    
    fprintf(fo, "%s[stack:%d locals:%d scope:%d-%d flags:%02x]\n", prefix, c->max_stack, c->local_count, c->init_scope_depth, c->max_scope_depth, c->method->flags);

    char prefix2[80];
    sprintf(prefix2, "%s    ", prefix);
    if(c->traits)
        traits_dump(fo, prefix, c->traits, file);
    fprintf(fo, "%s{\n", prefix);
    code_dump(c->code, c->exceptions, file, prefix2, fo);
    fprintf(fo, "%s}\n\n", prefix);
}

static void traits_free(trait_list_t*traits) 
{
    trait_list_t*t = traits;
    while(t) {
        if(t->trait->name) {
            multiname_destroy(t->trait->name);t->trait->name = 0;
        }
	if(t->trait->kind == TRAIT_SLOT || t->trait->kind == TRAIT_CONST) {
            multiname_destroy(t->trait->type_name);
        }
        if(t->trait->value) {
            constant_free(t->trait->value);t->trait->value = 0;
        }
        free(t->trait);t->trait = 0;
        t = t->next;
    }
    list_free(traits);
}

static trait_list_t* traits_parse(TAG*tag, pool_t*pool, abc_file_t*file)
{
    int num_traits = swf_GetU30(tag);
    trait_list_t*traits = list_new();
    int t;
    if(num_traits) {
        DEBUG printf("%d traits\n", num_traits);
    }
    
    for(t=0;t<num_traits;t++) {
        NEW(trait_t,trait);
	list_append(traits, trait);

	trait->name = multiname_clone(pool_lookup_multiname(pool, swf_GetU30(tag))); // always a QName (ns,name)

	const char*name = 0;
	DEBUG name = multiname_to_string(trait->name);
	U8 kind = swf_GetU8(tag);
        U8 attributes = kind&0xf0;
        kind&=0x0f;
        trait->kind = kind;
        trait->attributes = attributes;
	DEBUG printf("  trait %d) %s type=%02x\n", t, name, kind);
	if(kind == TRAIT_METHOD || kind == TRAIT_GETTER || kind == TRAIT_SETTER) { // method / getter / setter
	    trait->disp_id = swf_GetU30(tag);
	    trait->method = (abc_method_t*)array_getvalue(file->methods, swf_GetU30(tag));
	    DEBUG printf("  method/getter/setter\n");
	} else if(kind == TRAIT_FUNCTION) { // function
	    trait->slot_id =  swf_GetU30(tag);
	    trait->method = (abc_method_t*)array_getvalue(file->methods, swf_GetU30(tag));
	} else if(kind == TRAIT_CLASS) { // class
	    trait->slot_id = swf_GetU30(tag);
	    trait->cls = (abc_class_t*)array_getvalue(file->classes, swf_GetU30(tag));
	    DEBUG printf("  class %s %d %d\n", name, trait->slot_id, trait->cls);
	} else if(kind == TRAIT_SLOT || kind == TRAIT_CONST) { // slot, const
            /* a slot is a variable in a class that is shared amonst all instances
               of the same type, but which has a unique location in each object 
               (in other words, slots are non-static, traits are static)
             */
	    trait->slot_id = swf_GetU30(tag);
            trait->type_name = multiname_clone(pool_lookup_multiname(pool, swf_GetU30(tag)));
	    int vindex = swf_GetU30(tag);
	    if(vindex) {
		int vkind = swf_GetU8(tag);
                trait->value = constant_fromindex(pool, vindex, vkind);
	    }
	    DEBUG printf("  slot %s %d %s (%s)\n", name, trait->slot_id, trait->type_name->name, constant_to_string(trait->value));
	} else {
	    fprintf(stderr, "Can't parse trait type %d\n", kind);
	}
        if(attributes&0x40) {
            int num = swf_GetU30(tag);
            int s;
            for(s=0;s<num;s++) {
                swf_GetU30(tag); //index into metadata array
            }
        }
    }
    return traits;
}

void traits_skip(TAG*tag)
{
    int num_traits = swf_GetU30(tag);
    int t;
    for(t=0;t<num_traits;t++) {
        swf_GetU30(tag);
	U8 kind = swf_GetU8(tag);
        U8 attributes = kind&0xf0;
        kind&=0x0f;
        swf_GetU30(tag);
        swf_GetU30(tag);
	if(kind == TRAIT_SLOT || kind == TRAIT_CONST) {
	    if(swf_GetU30(tag)) swf_GetU8(tag);
        } else if(kind>TRAIT_CONST) {
	    fprintf(stderr, "Can't parse trait type %d\n", kind);
	}
        if(attributes&0x40) {
            int s, num = swf_GetU30(tag);
            for(s=0;s<num;s++) swf_GetU30(tag);
        }
    }
}


static void traits_write(pool_t*pool, TAG*tag, trait_list_t*traits)
{
    if(!traits) {
        swf_SetU30(tag, 0);
        return;
    }
    swf_SetU30(tag, list_length(traits));
    int s;

    while(traits) {
	trait_t*trait = traits->trait;

	swf_SetU30(tag, pool_register_multiname(pool, trait->name));
	swf_SetU8(tag, trait->kind|trait->attributes);

	swf_SetU30(tag, trait->data1);

        if(trait->kind == TRAIT_CLASS) {
            swf_SetU30(tag, trait->cls->index);
        } else if(trait->kind == TRAIT_GETTER ||
                  trait->kind == TRAIT_SETTER ||
                  trait->kind == TRAIT_METHOD) {
            swf_SetU30(tag, trait->method->index);
        } else if(trait->kind == TRAIT_SLOT ||
                  trait->kind == TRAIT_CONST) {
            int index = pool_register_multiname(pool, trait->type_name);
            swf_SetU30(tag, index);
        } else  {
	    swf_SetU30(tag, trait->data2);
        }

	if(trait->kind == TRAIT_SLOT || trait->kind == TRAIT_CONST) {
            int vindex = constant_get_index(pool, trait->value);
	    swf_SetU30(tag, vindex);
	    if(vindex) {
		swf_SetU8(tag, trait->value->type);
	    }
	}
        if(trait->attributes&0x40) {
            // metadata
            swf_SetU30(tag, 0);
        }
        traits = traits->next;
    }
}


static void traits_dump(FILE*fo, const char*prefix, trait_list_t*traits, abc_file_t*file)
{
    int t;
    while(traits) {
	trait_t*trait = traits->trait;
	char*name = multiname_to_string(trait->name);
	U8 kind = trait->kind;
        U8 attributes = trait->attributes;
	if(kind == TRAIT_METHOD) {
            abc_method_t*m = trait->method;
	    dump_method(fo, prefix, "method", name, m, file);
	} else if(kind == TRAIT_GETTER) {
            abc_method_t*m = trait->method;
	    dump_method(fo, prefix, "getter", name, m, file);
        } else if(kind == TRAIT_SETTER) {
            abc_method_t*m = trait->method;
	    dump_method(fo, prefix, "setter", name, m, file);
	} else if(kind == TRAIT_FUNCTION) { // function
            abc_method_t*m = trait->method;
	    dump_method(fo, prefix, "function", name, m, file);
	} else if(kind == TRAIT_CLASS) { // class
            abc_class_t*cls = trait->cls;
            if(!cls) {
	        fprintf(fo, "%sslot %d: class %s=00000000\n", prefix, trait->slot_id, name);
            } else {
	        fprintf(fo, "%sslot %d: class %s=%s\n", prefix, trait->slot_id, name, cls->classname->name);
            }
	} else if(kind == TRAIT_SLOT || kind == TRAIT_CONST) { // slot, const
	    int slot_id = trait->slot_id;
	    char*type_name = multiname_to_string(trait->type_name);
            char*value = constant_to_string(trait->value);
	    fprintf(fo, "%sslot %d: %s%s %s %s %s\n", prefix, trait->slot_id, 
                    kind==TRAIT_CONST?"const ":"", type_name, name, 
                    value?"=":"", value);
            if(value) free(value);
            free(type_name);
	} else {
	    fprintf(fo, "%s    can't dump trait type %d\n", prefix, kind);
	}
        free(name);
        traits=traits->next;
    }
}

void* swf_DumpABC(FILE*fo, void*code, char*prefix)
{
    abc_file_t* file = (abc_file_t*)code;
        
    if(file->name) {
        fprintf(fo, "%s#\n", prefix);
        fprintf(fo, "%s#name: %s\n", prefix, file->name);
        fprintf(fo, "%s#\n", prefix);
    }

    int t;
    for(t=0;t<file->metadata->num;t++) {
        const char*entry_name = array_getkey(file->metadata, t);
        fprintf(fo, "%s#Metadata \"%s\":\n", prefix, entry_name);
        int s;
        array_t*items = (array_t*)array_getvalue(file->metadata, t);
        for(s=0;s<items->num;s++) {
            fprintf(fo, "%s#  %s=%s\n", prefix, array_getkey(items, s), array_getvalue(items,s));
        }
        fprintf(fo, "%s#\n", prefix);
    }

    for(t=0;t<file->classes->num;t++) {
        abc_class_t*cls = (abc_class_t*)array_getvalue(file->classes, t);
        char prefix2[80];
        sprintf(prefix2, "%s    ", prefix);

        fprintf(fo, "%s", prefix);
        if(cls->flags&1) fprintf(fo, "sealed ");
        if(cls->flags&2) fprintf(fo, "final ");
        if(cls->flags&4) fprintf(fo, "interface ");
        if(cls->flags&8) {
            char*s = namespace_to_string(cls->protectedNS);
            fprintf(fo, "protectedNS(%s) ", s);
            free(s);
        }

        char*classname = multiname_to_string(cls->classname);
	fprintf(fo, "class %s", classname);
        free(classname);
        if(cls->superclass) {
            char*supername = multiname_to_string(cls->superclass);
            fprintf(fo, " extends %s", supername);
            free(supername);
            multiname_list_t*ilist = cls->interfaces;
            if(ilist)
                fprintf(fo, " implements");
            while(ilist) {
                char*s = multiname_to_string(ilist->multiname);
                fprintf(fo, " %s", s);
                free(s);
                ilist = ilist->next;
            }
            ilist->next;
        }
        if(cls->flags&0xf0) 
            fprintf(fo, "extra flags=%02x\n", cls->flags&0xf0);
	fprintf(fo, "%s{\n", prefix);

        if(cls->static_constructor)
            dump_method(fo, prefix2,"staticconstructor", "", cls->static_constructor, file);
        traits_dump(fo, prefix2, cls->static_constructor_traits, file);
	
        char*n = multiname_to_string(cls->classname);
        if(cls->constructor)
	    dump_method(fo, prefix2, "constructor", n, cls->constructor, file);
        free(n);
	traits_dump(fo, prefix2,cls->traits, file);
        fprintf(fo, "%s}\n", prefix);
    }
    fprintf(fo, "%s\n", prefix);

    for(t=0;t<file->scripts->num;t++) {
        abc_script_t*s = (abc_script_t*)array_getvalue(file->scripts, t);
        dump_method(fo, prefix,"initmethod", "init", s->method, file);
        traits_dump(fo, prefix, s->traits, file);
    }
    return file;
}

void* swf_ReadABC(TAG*tag)
{
    abc_file_t* file = abc_file_new();
    pool_t*pool = pool_new();

    swf_SetTagPos(tag, 0);
    int t;
    if(tag->id == ST_DOABC) {
        U32 abcflags = swf_GetU32(tag);
        DEBUG printf("flags=%08x\n", abcflags);
        char*name= swf_GetString(tag);
        file->name = (name&&name[0])?strdup(name):0;
    }
    U32 version = swf_GetU32(tag);
    if(version!=0x002e0010) {
        fprintf(stderr, "Warning: unknown AVM2 version %08x\n", version);
    }

    pool_read(pool, tag);

    int num_methods = swf_GetU30(tag);
    DEBUG printf("%d methods\n", num_methods);
    for(t=0;t<num_methods;t++) {
	NEW(abc_method_t,m);
	int param_count = swf_GetU30(tag);
	int return_type_index = swf_GetU30(tag);
        if(return_type_index)
            m->return_type = multiname_clone(pool_lookup_multiname(pool, return_type_index));
        else
            m->return_type = 0;

	int s;
	for(s=0;s<param_count;s++) {
	    int type_index = swf_GetU30(tag);
            
            /* type_index might be 0, which probably means "..." (varargs) */
            multiname_t*param = type_index?multiname_clone(pool_lookup_multiname(pool, type_index)):0;
            list_append(m->parameters, param);
        }

	int namenr = swf_GetU30(tag);
	if(namenr)
	    m->name = strdup(pool_lookup_string(pool, namenr));
        else
	    m->name = strdup("");

	m->flags = swf_GetU8(tag);
	
        DEBUG printf("method %d) %s flags=%02x\n", t, params_to_string(m->parameters), m->flags);

        if(m->flags&0x08) {
            /* TODO optional parameters */
            m->optional_parameters = list_new();
            int num = swf_GetU30(tag);
            int s;
            for(s=0;s<num;s++) {
                int vindex = swf_GetU30(tag);
                U8 vkind = swf_GetU8(tag); // specifies index type for "val"
                constant_t*c = constant_fromindex(pool, vindex, vkind);
                list_append(m->optional_parameters, c);
            }
        }
	if(m->flags&0x80) {
            /* debug information- not used by avm2 */
            multiname_list_t*l = m->parameters;
            while(l) {
	        char*name = pool_lookup_string(pool, swf_GetU30(tag));
                l = l->next;
            }
	}
	array_append(file->methods, NO_KEY, m);
    }
            
    parse_metadata(tag, file, pool);
	
    /* skip classes, and scripts for now, and do the real parsing later */
    int num_classes = swf_GetU30(tag);
    int classes_pos = tag->pos;
    DEBUG printf("%d classes\n", num_classes);
    for(t=0;t<num_classes;t++) {
	abc_class_t*cls = malloc(sizeof(abc_class_t));
	memset(cls, 0, sizeof(abc_class_t));
	
        DEBUG printf("class %d\n", t);
	swf_GetU30(tag); //classname
	swf_GetU30(tag); //supername

        array_append(file->classes, NO_KEY, cls);

	cls->flags = swf_GetU8(tag);
	if(cls->flags&8) 
	    swf_GetU30(tag); //protectedNS
        int s;
	int inum = swf_GetU30(tag); //interface count
        cls->interfaces = 0;
        for(s=0;s<inum;s++) {
            int interface_index = swf_GetU30(tag);
            multiname_t* m = multiname_clone(pool_lookup_multiname(pool, interface_index));
            list_append(cls->interfaces, m);
            DEBUG printf("  class %d interface: %s\n", t, m->name);
        }

	swf_GetU30(tag); //iinit
	traits_skip(tag);
    }
    for(t=0;t<num_classes;t++) {
        abc_class_t*cls = (abc_class_t*)array_getvalue(file->classes, t);
	int cinit = swf_GetU30(tag);
        cls->static_constructor = (abc_method_t*)array_getvalue(file->methods, cinit);
        traits_skip(tag);
    }
    int num_scripts = swf_GetU30(tag);
    DEBUG printf("%d scripts\n", num_scripts);
    for(t=0;t<num_scripts;t++) {
	int init = swf_GetU30(tag);
        traits_skip(tag);
    }

    int num_method_bodies = swf_GetU30(tag);
    DEBUG printf("%d method bodies\n", num_method_bodies);
    for(t=0;t<num_method_bodies;t++) {
	int methodnr = swf_GetU30(tag);
	if(methodnr >= file->methods->num) {
	    printf("Invalid method number: %d\n", methodnr);
	    return 0;
	}
	abc_method_t*m = (abc_method_t*)array_getvalue(file->methods, methodnr);
	abc_method_body_t*c = malloc(sizeof(abc_method_body_t));
	memset(c, 0, sizeof(abc_method_body_t));
	c->max_stack = swf_GetU30(tag);
	c->local_count = swf_GetU30(tag);
	c->init_scope_depth = swf_GetU30(tag);
	c->max_scope_depth = swf_GetU30(tag);
	int code_length = swf_GetU30(tag);

	c->method = m;
	m->body = c;

        int pos = tag->pos + code_length;
        codelookup_t*codelookup = 0;
        c->code = code_parse(tag, code_length, file, pool, &codelookup);
        tag->pos = pos;

	int exception_count = swf_GetU30(tag);
        int s;
        c->exceptions = list_new();
        for(s=0;s<exception_count;s++) {
            exception_t*e = malloc(sizeof(exception_t));

            e->from = code_atposition(codelookup, swf_GetU30(tag));
            e->to = code_atposition(codelookup, swf_GetU30(tag));
            e->target = code_atposition(codelookup, swf_GetU30(tag));

            e->exc_type = multiname_clone(pool_lookup_multiname(pool, swf_GetU30(tag)));
            e->var_name = multiname_clone(pool_lookup_multiname(pool, swf_GetU30(tag)));
            //e->var_name = pool_lookup_string(pool, swf_GetU30(tag));
            //if(e->var_name) e->var_name = strdup(e->var_name);
            list_append(c->exceptions, e);
        }
        codelookup_free(codelookup);
	c->traits = traits_parse(tag, pool, file);

	DEBUG printf("method_body %d) (method %d), %d bytes of code", t, methodnr, code_length);

	array_append(file->method_bodies, NO_KEY, c);
    }
    if(tag->len - tag->pos) {
	fprintf(stderr, "%d unparsed bytes remaining in ABC block\n", tag->len - tag->pos);
	return 0;
    }

    swf_SetTagPos(tag, classes_pos);
    for(t=0;t<num_classes;t++) {
        abc_class_t*cls = (abc_class_t*)array_getvalue(file->classes, t);

        int classname_index = swf_GetU30(tag);
        int superclass_index = swf_GetU30(tag);
	cls->classname = multiname_clone(pool_lookup_multiname(pool, classname_index));
	cls->superclass = multiname_clone(pool_lookup_multiname(pool, superclass_index));
	cls->flags = swf_GetU8(tag);
	const char*ns = "";
	if(cls->flags&8) {
            int ns_index = swf_GetU30(tag);
	    cls->protectedNS = namespace_clone(pool_lookup_namespace(pool, ns_index));
	}
        
        int num_interfaces = swf_GetU30(tag); //interface count
        int s;
        for(s=0;s<num_interfaces;s++) {
            swf_GetU30(tag); // multiname index TODO
        }
	int iinit = swf_GetU30(tag);
        cls->constructor = (abc_method_t*)array_getvalue(file->methods, iinit);
	cls->traits = traits_parse(tag, pool, file);
    }
    for(t=0;t<num_classes;t++) {
        abc_class_t*cls = (abc_class_t*)array_getvalue(file->classes, t);
        /* SKIP */
	swf_GetU30(tag); // cindex
	cls->static_constructor_traits = traits_parse(tag, pool, file);
    }
    int num_scripts2 = swf_GetU30(tag);
    for(t=0;t<num_scripts2;t++) {
	int init = swf_GetU30(tag);
        abc_method_t*m = (abc_method_t*)array_getvalue(file->methods, init);
        
        abc_script_t*s = malloc(sizeof(abc_script_t));
        memset(s, 0, sizeof(abc_script_t));
        s->method = m;
        s->traits = traits_parse(tag, pool, file);
        array_append(file->scripts, NO_KEY, s);
        if(!s->traits) {
	    fprintf(stderr, "Can't parse script traits\n");
            return 0;
        }
    }

    pool_destroy(pool);
    return file;
}

void swf_WriteABC(TAG*abctag, void*code)
{
    abc_file_t*file = (abc_file_t*)code;
    pool_t*pool = pool_new();

    TAG*tmp = swf_InsertTag(0,0);
    TAG*tag = tmp;
    int t;
   
    char need_null_method=0;
    for(t=0;t<file->classes->num;t++) {
	abc_class_t*c = (abc_class_t*)array_getvalue(file->classes, t);
        if(!c->constructor || !c->static_constructor) {
            need_null_method=1;
            break;
        }
    }

    abc_method_t*nullmethod = 0;
    if(need_null_method) {
        nullmethod = malloc(sizeof(abc_method_t));
        memset(nullmethod, 0, sizeof(abc_method_t));
        /*TODO: might be more efficient to have this at the beginning */
        array_append(file->methods, NO_KEY, nullmethod);
    }
   
    swf_SetU30(tag, file->methods->num);
    /* enumerate classes, methods and method bodies */
    for(t=0;t<file->methods->num;t++) {
	abc_method_t*m = (abc_method_t*)array_getvalue(file->methods, t);
        m->index = t;
    }
    for(t=0;t<file->classes->num;t++) {
	abc_class_t*c = (abc_class_t*)array_getvalue(file->classes, t);
        c->index = t;
    }
    for(t=0;t<file->method_bodies->num;t++) {
        abc_method_body_t*m = (abc_method_body_t*)array_getvalue(file->method_bodies, t);
        m->index = t;
    }
    
    /* generate code statistics */
    for(t=0;t<file->method_bodies->num;t++) {
        abc_method_body_t*m = (abc_method_body_t*)array_getvalue(file->method_bodies, t);
        m->stats = code_get_statistics(m->code, m->exceptions);
    }
    
    for(t=0;t<file->methods->num;t++) {
	abc_method_t*m = (abc_method_t*)array_getvalue(file->methods, t);
        int n = 0;
        multiname_list_t*l = m->parameters;
        int num_params = list_length(m->parameters);
	swf_SetU30(tag, num_params);
        if(m->return_type) 
	    swf_SetU30(tag, pool_register_multiname(pool, m->return_type));
        else
	    swf_SetU30(tag, 0);
	int s;
        while(l) {
	    swf_SetU30(tag, pool_register_multiname(pool, l->multiname));
            l = l->next;
	}
        if(m->name) {
	    swf_SetU30(tag, pool_register_string(pool, m->name));
        } else {
	    swf_SetU30(tag, 0);
        }

        U8 flags = m->flags&(METHOD_NEED_REST|METHOD_NEED_ARGUMENTS);
        if(m->optional_parameters)
            flags |= METHOD_HAS_OPTIONAL;
        if(m->body) {
            flags |= m->body->stats->flags;
        }

	swf_SetU8(tag, flags);
        if(flags&METHOD_HAS_OPTIONAL) {
            swf_SetU30(tag, list_length(m->optional_parameters));
            constant_list_t*l = m->optional_parameters;
            while(l) {
                swf_SetU30(tag, constant_get_index(pool, l->constant));
                swf_SetU8(tag, l->constant->type);
                l = l->next;
            }
        }
    }
   
    /* write metadata */
    swf_SetU30(tag, file->metadata->num);
    for(t=0;t<file->metadata->num;t++) {
        const char*entry_name = array_getkey(file->metadata, t);
        swf_SetU30(tag, pool_register_string(pool, entry_name));
        array_t*items = (array_t*)array_getvalue(file->metadata, t);
        swf_SetU30(tag, items->num);
        int s;
        for(s=0;s<items->num;s++) {
            int i1 = pool_register_string(pool, array_getkey(items, s));
            int i2 = pool_register_string(pool, array_getvalue(items, s));
            swf_SetU30(tag, i1);
            swf_SetU30(tag, i2);
        }
    }

    swf_SetU30(tag, file->classes->num);
    for(t=0;t<file->classes->num;t++) {
	abc_class_t*c = (abc_class_t*)array_getvalue(file->classes, t);
   
        int classname_index = pool_register_multiname(pool, c->classname);
        int superclass_index = pool_register_multiname(pool, c->superclass);

	swf_SetU30(tag, classname_index);
	swf_SetU30(tag, superclass_index);

	swf_SetU8(tag, c->flags); // flags
        if(c->flags&0x08) {
            int ns_index = pool_register_namespace(pool, c->protectedNS);
	    swf_SetU30(tag, ns_index);
        }

        swf_SetU30(tag, list_length(c->interfaces));
        multiname_list_t*interface= c->interfaces;
        while(interface) {
            swf_SetU30(tag, pool_register_multiname(pool, interface->multiname));
            interface = interface->next;
        }

	if(!c->constructor) {
            swf_SetU30(tag, nullmethod->index);
	} else {
	    swf_SetU30(tag, c->constructor->index);
        }
	traits_write(pool, tag, c->traits);
    }
    for(t=0;t<file->classes->num;t++) {
	abc_class_t*c = (abc_class_t*)array_getvalue(file->classes, t);
	if(!c->static_constructor) {
            swf_SetU30(tag, nullmethod->index);
	} else {
	    swf_SetU30(tag, c->static_constructor->index);
        }
        traits_write(pool, tag, c->static_constructor_traits);
    }

    swf_SetU30(tag, file->scripts->num);
    for(t=0;t<file->scripts->num;t++) {
	abc_script_t*s = (abc_script_t*)array_getvalue(file->scripts, t);
	swf_SetU30(tag, s->method->index); //!=t!
	traits_write(pool, tag, s->traits);
    }

    swf_SetU30(tag, file->method_bodies->num);
    for(t=0;t<file->method_bodies->num;t++) {
	abc_method_body_t*c = (abc_method_body_t*)array_getvalue(file->method_bodies, t);
	abc_method_t*m = c->method;
	swf_SetU30(tag, m->index);

	//swf_SetU30(tag, c->max_stack);
	//swf_SetU30(tag, c->local_count);
	//swf_SetU30(tag, c->init_scope_depth);
	//swf_SetU30(tag, c->max_scope_depth);

	swf_SetU30(tag, c->stats->max_stack);
        if(list_length(c->method->parameters)+1 <= c->stats->local_count)
	    swf_SetU30(tag, c->stats->local_count);
        else
	    swf_SetU30(tag, list_length(c->method->parameters)+1);
	swf_SetU30(tag, c->init_scope_depth);
	swf_SetU30(tag, c->stats->max_scope_depth+
                        c->init_scope_depth);

        code_write(tag, c->code, pool, file);

	swf_SetU30(tag, list_length(c->exceptions));
        exception_list_t*l = c->exceptions;
        while(l) {
            // warning: assumes "pos" in each code_t is up-to-date
            swf_SetU30(tag, l->exception->from->pos);
            swf_SetU30(tag, l->exception->to->pos);
            swf_SetU30(tag, l->exception->target->pos);
            swf_SetU30(tag, pool_register_multiname(pool, l->exception->exc_type));
            swf_SetU30(tag, pool_register_multiname(pool, l->exception->var_name));
            l = l->next;
        }

        traits_write(pool, tag, c->traits);
    }
   
    /* free temporary codestat data again. Notice: If we were to write this
       file multiple times, this can also be shifted to abc_file_free() */
    for(t=0;t<file->method_bodies->num;t++) {
        abc_method_body_t*m = (abc_method_body_t*)array_getvalue(file->method_bodies, t);
        codestats_free(m->stats);m->stats=0;
    }

    // --- start to write real tag --
    
    tag = abctag;

    if(tag->id == ST_DOABC) {
        swf_SetU32(tag, 1); // flags
        swf_SetString(tag, file->name);
    }

    swf_SetU16(tag, 0x10); //version
    swf_SetU16(tag, 0x2e);
    
    pool_write(pool, tag);
    
    swf_SetBlock(tag, tmp->data, tmp->len);

    swf_DeleteTag(0, tmp);
    pool_destroy(pool);
}

void abc_file_free(abc_file_t*file)
{
    int t;
    for(t=0;t<file->metadata->num;t++) {
        array_t*items = (array_t*)array_getvalue(file->metadata, t);
        int s;
        for(s=0;s<items->num;s++) {
            free(array_getvalue(items, s));
        }
        array_free(items);
    }
    array_free(file->metadata);

    for(t=0;t<file->methods->num;t++) {
        abc_method_t*m = (abc_method_t*)array_getvalue(file->methods, t);

        multiname_list_t*param = m->parameters;
        while(param) {
            multiname_destroy(param->multiname);param->multiname=0;
            param = param->next;
        }
        list_free(m->parameters);m->parameters=0;
       
        constant_list_t*opt = m->optional_parameters;
        while(opt) {
            constant_free(opt->constant);opt->constant=0;
            opt = opt->next;
        }
        list_free(m->optional_parameters);m->optional_parameters=0;

        if(m->name) {
            free((void*)m->name);m->name=0;
        }
        if(m->return_type) {
            multiname_destroy(m->return_type);
        }
        free(m);
    }
    array_free(file->methods);

    for(t=0;t<file->classes->num;t++) {
        abc_class_t*cls = (abc_class_t*)array_getvalue(file->classes, t);
        traits_free(cls->traits);cls->traits=0;
	traits_free(cls->static_constructor_traits);cls->static_constructor_traits=0;

        if(cls->classname) {
            multiname_destroy(cls->classname);
        }
        if(cls->superclass) {
            multiname_destroy(cls->superclass);
        }

        multiname_list_t*i = cls->interfaces;
        while(i) {
            multiname_destroy(i->multiname);i->multiname=0;
            i = i->next;
        }
        list_free(cls->interfaces);cls->interfaces=0;

        if(cls->protectedNS) {
	    namespace_destroy(cls->protectedNS);
        }
        free(cls);
    }
    array_free(file->classes);

    for(t=0;t<file->scripts->num;t++) {
        abc_script_t*s = (abc_script_t*)array_getvalue(file->scripts, t);
        traits_free(s->traits);s->traits=0;
        free(s);
    }
    array_free(file->scripts);

    for(t=0;t<file->method_bodies->num;t++) {
        abc_method_body_t*body = (abc_method_body_t*)array_getvalue(file->method_bodies, t);
        code_free(body->code);body->code=0;
	traits_free(body->traits);body->traits=0;

        exception_list_t*ee = body->exceptions;
        while(ee) {
            exception_t*e=ee->exception;ee->exception=0;
            e->from = e->to = e->target = 0;
            multiname_destroy(e->exc_type);e->exc_type=0;
            multiname_destroy(e->var_name);e->var_name=0;
            free(e);
            ee=ee->next;
        }
        list_free(body->exceptions);body->exceptions=0;
        
        free(body);
    }
    array_free(file->method_bodies);

    if(file->name) {
        free((void*)file->name);file->name=0;
    }

    free(file);
}

void swf_FreeABC(void*code)
{
    abc_file_t*file= (abc_file_t*)code;
    abc_file_free(file);
}

void swf_AddButtonLinks(SWF*swf, char stop_each_frame, char events)
{
    int num_frames = 0;
    int has_buttons = 0;
    TAG*tag=swf->firstTag;
    while(tag) {
        if(tag->id == ST_SHOWFRAME)
            num_frames++;
        if(tag->id == ST_DEFINEBUTTON || tag->id == ST_DEFINEBUTTON2)
            has_buttons = 1;
        tag = tag->next;
    }

    abc_file_t*file = abc_file_new();
    abc_method_body_t*c = 0;
   
    abc_class_t*cls = abc_class_new2(file, "rfx::MainTimeline", "flash.display::MovieClip");
    abc_class_protectedNS(cls, "rfx:MainTimeline");
  
    TAG*abctag = swf_InsertTagBefore(swf, swf->firstTag, ST_DOABC);
    
    tag = swf_InsertTag(abctag, ST_SYMBOLCLASS);
    swf_SetU16(tag, 1);
    swf_SetU16(tag, 0);
    swf_SetString(tag, "rfx.MainTimeline");

    c = abc_class_staticconstructor(cls, 0, 0);
    c->max_stack = 1;
    c->local_count = 1;
    c->init_scope_depth = 9;
    c->max_scope_depth = 10;

    __ getlocal_0(c);
    __ pushscope(c);
    __ returnvoid(c);

    c = abc_class_constructor(cls, 0, 0);
    c->max_stack = 3;
    c->local_count = 1;
    c->init_scope_depth = 10;
    c->max_scope_depth = 11;
    
    debugfile(c, "constructor.as");

    __ getlocal_0(c);
    __ pushscope(c);

    __ getlocal_0(c);
    __ constructsuper(c,0);

    __ getlex(c, "[package]flash.system::Security");
    __ pushstring(c, "*");
    __ callpropvoid(c, "[package]::allowDomain", 1);
    
    if(stop_each_frame || has_buttons) {
        int frame = 0;
        tag = swf->firstTag;
        abc_method_body_t*f = 0; //frame script
        while(tag && tag->id!=ST_END) {
            char framename[80];
            char needs_framescript=0;
            char buttonname[80];
            char functionname[80];
            sprintf(framename, "[packageinternal]rfx::frame%d", frame);
            
            if(!f && (tag->id == ST_DEFINEBUTTON || tag->id == ST_DEFINEBUTTON2 || stop_each_frame)) {
                /* make the contructor add a frame script */
                __ findpropstrict(c,"[package]::addFrameScript");
                __ pushbyte(c,frame);
                __ getlex(c,framename);
                __ callpropvoid(c,"[package]::addFrameScript",2);

                f = abc_class_method(cls, 0, framename, 0);
                f->max_stack = 3;
                f->local_count = 1;
                f->init_scope_depth = 10;
                f->max_scope_depth = 11;
                __ debugfile(f, "framescript.as");
                __ debugline(f, 1);
                __ getlocal_0(f);
                __ pushscope(f);
            }

            if(tag->id == ST_DEFINEBUTTON || tag->id == ST_DEFINEBUTTON2) {
                U16 id = swf_GetDefineID(tag);
                sprintf(buttonname, "::button%d", swf_GetDefineID(tag));
                __ getlex(f,buttonname);
                __ getlex(f,"flash.events::MouseEvent");
                __ getproperty(f, "::CLICK");
                sprintf(functionname, "::clickbutton%d", swf_GetDefineID(tag));
                __ getlex(f,functionname);
                __ callpropvoid(f, "::addEventListener" ,2);

                if(stop_each_frame) {
                    __ findpropstrict(f, "[package]::stop");
                    __ callpropvoid(f, "[package]::stop", 0);
                }
                needs_framescript = 1;

                abc_method_body_t*h =
                    abc_class_method(cls, "::void", functionname, 1, "flash.events::MouseEvent");
                h->max_stack = 6;
                h->local_count = 2;
                h->init_scope_depth = 10;
                h->max_scope_depth = 11;
                __ getlocal_0(h);
                __ pushscope(h);

                ActionTAG*oldaction = swf_ButtonGetAction(tag);
                if(oldaction && oldaction->op == ACTION__GOTOFRAME) {
                    int framenr = GET16(oldaction->data);
                    if(framenr>254) {
                        fprintf(stderr, "Warning: Couldn't translate jump to frame %d to flash 9 actionscript\n", framenr);
                    }
                    if(!events) {
                        __ findpropstrict(h,"[package]::gotoAndStop");
                        __ pushbyte(h,framenr+1);
                        __ callpropvoid(h,"[package]::gotoAndStop", 1);
                    } else {
                        char framename[80];
                        sprintf(framename, "frame%d", framenr);
                        __ getlocal_0(h); //this
                        __ findpropstrict(h, "[package]flash.events::TextEvent");
                        __ pushstring(h, "link");
                        __ pushtrue(h);
                        __ pushtrue(h);
                        __ pushstring(h, framename);
                        __ constructprop(h,"[package]flash.events::TextEvent", 4);
                        __ callpropvoid(h,"[package]::dispatchEvent", 1);
                    }
                } else if(oldaction && oldaction->op == ACTION__GETURL) {
                    if(!events) {
                        __ findpropstrict(h,"flash.net::navigateToURL");
                        __ findpropstrict(h,"flash.net::URLRequest");
                        // TODO: target _blank
                        __ pushstring(h,oldaction->data); //url
                        __ constructprop(h,"flash.net::URLRequest", 1);
                        __ callpropvoid(h,"flash.net::navigateToURL", 1);
                    } else {
                        __ getlocal_0(h); //this
                        __ findpropstrict(h, "[package]flash.events::TextEvent");
                        __ pushstring(h, "link");
                        __ pushtrue(h);
                        __ pushtrue(h);
                        __ pushstring(h,oldaction->data); //url
                        __ constructprop(h,"[package]flash.events::TextEvent", 4);
                        __ callpropvoid(h,"[package]::dispatchEvent", 1);
                    }
                } else if(oldaction) {
                    fprintf(stderr, "Warning: Couldn't translate button code of button %d to flash 9 abc action\n", id);
                }
                __ returnvoid(h);
                swf_ActionFree(oldaction);
            }
            if(tag->id == ST_SHOWFRAME) {
                if(f) {
                    __ returnvoid(f);
                    f = 0;
                }
                frame++;
            }
            tag = tag->next;
        }
        if(f) {
            __ returnvoid(f);
        }
    }
    __ returnvoid(c);

    tag = swf->firstTag;
    while(tag) {
        if(tag->id == ST_DEFINEBUTTON || tag->id == ST_DEFINEBUTTON2) {
            char buttonname[80];
            sprintf(buttonname, "::button%d", swf_GetDefineID(tag));
            abc_AddSlot(cls, buttonname, 0, "flash.display::SimpleButton");
        }
        tag = tag->next;
    }


    abc_script_t*s = abc_initscript(file, 0, 0);
    c = s->method->body;
    c->max_stack = 2;
    c->local_count = 1;
    c->init_scope_depth = 1;
    c->max_scope_depth = 9;

    __ getlocal_0(c);
    __ pushscope(c);
    __ getscopeobject(c, 0);
    __ getlex(c,"::Object");
    __ pushscope(c);
    __ getlex(c,"flash.events::EventDispatcher");
    __ pushscope(c);
    __ getlex(c,"flash.display::DisplayObject");
    __ pushscope(c);
    __ getlex(c,"flash.display::InteractiveObject");
    __ pushscope(c);
    __ getlex(c,"flash.display::DisplayObjectContainer");
    __ pushscope(c);
    __ getlex(c,"flash.display::Sprite");
    __ pushscope(c);
    __ getlex(c,"flash.display::MovieClip");
    __ pushscope(c);
    __ getlex(c,"flash.display::MovieClip");
    __ newclass(c,cls);
    __ popscope(c);
    __ popscope(c);
    __ popscope(c);
    __ popscope(c);
    __ popscope(c);
    __ popscope(c);
    __ popscope(c);
    __ initproperty(c,"rfx::MainTimeline");
    __ returnvoid(c);

    //abc_method_body_addClassTrait(c, "rfx:MainTimeline", 1, cls);
    multiname_t*classname = multiname_fromstring("rfx::MainTimeline");
    abc_initscript_addClassTrait(s, classname, cls);
    multiname_destroy(classname);

    swf_WriteABC(abctag, file);
}
