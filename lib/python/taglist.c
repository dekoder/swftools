#include <Python.h>
#undef HAVE_STAT
#include "../rfxswf.h"
#include "../log.h"
#include "./pyutils.h"
#include "primitives.h"
#include "action.h"
#include "tag.h"
#include "tagmap.h"
#include "taglist.h"

//----------------------------------------------------------------------------
typedef struct {
    PyObject_HEAD
    PyObject* taglist;
    PyObject* tagmap;
} TagListObject;
//----------------------------------------------------------------------------
PyObject * taglist_new()
{
    TagListObject* taglist = PyObject_New(TagListObject, &TagListClass);
    mylog("+%08x(%d) taglist_new", (int)taglist, taglist->ob_refcnt);
    taglist->tagmap = tagmap_new();
    taglist->taglist = PyList_New(0);
    return (PyObject*)taglist;
}
//----------------------------------------------------------------------------
PyObject * taglist_new2(TAG*tag)
{
    TagListObject* taglist = PyObject_New(TagListObject, &TagListClass);
    mylog("+%08x(%d) taglist_new2 tag=%08x", (int)taglist, taglist->ob_refcnt, tag);
    taglist->tagmap = tagmap_new();

    int nr=0;
    TAG*t = tag;
    while(t) {nr++;t=t->next;}
    taglist->taglist = PyList_New(nr);
    
    mylog("+%08x(%d) taglist_new2: %d items", (int)taglist, taglist->ob_refcnt, nr);

    nr = 0;
    t = tag;
    while(t) {
	PyObject*newtag = tag_new2(t, taglist->tagmap);
	if(newtag==NULL) {
	    // pass through exception
	    return NULL;
	}
	PyList_SET_ITEM(taglist->taglist,nr,newtag);Py_INCREF(newtag);
	if(swf_isDefiningTag(t)) {
	    int id = swf_GetDefineID(t);
	    tagmap_addMapping(taglist->tagmap, id, newtag);
	}
	nr++;
	t=t->next;
    }
    return (PyObject*)taglist;
}
//----------------------------------------------------------------------------
TAG* taglist_getTAGs(PyObject*self)
{
    if(!PY_CHECK_TYPE(self,&TagListClass)) {
	PyErr_SetString(PyExc_Exception, setError("Not a taglist (%08x).", self));
	return 0;
    }
    TagListObject*taglist = (TagListObject*)self;

    /* TODO: the tags will be modified by this. We should set mutexes. */
    
    int l = PyList_Size(taglist->taglist);
    int t;
    TAG* tag = 0;
    TAG* firstTag = 0;
    mylog(" %08x(%d) taglist_getTAGs", (int)self, self->ob_refcnt);
    for(t=0;t<l;t++) {
	PyObject*item = PyList_GetItem(taglist->taglist, t);
	tag = tag_getTAG(item, tag, taglist->tagmap);
	if(!firstTag)
	    firstTag = tag;
	mylog(" %08x(%d) taglist_getTAGs: added tag %08x", (int)self, self->ob_refcnt, tag);
    }
    return firstTag;
}
//----------------------------------------------------------------------------
static PyObject * taglist_foldAll(PyObject* self, PyObject* args)
{
/*    SWF swf;
    TagListObject*taglist = (TagListObject*)self;
    if(!self || !PyArg_ParseTuple(args,"")) 
	return NULL;
    swf.firstTag = taglist->firstTag;
    swf_FoldAll(&swf);
    taglist->firstTag = swf.firstTag;
    taglist->lastTag = 0; // FIXME
    taglist->searchTag = 0;*/
    return PY_NONE;
}
//----------------------------------------------------------------------------
static PyObject * taglist_unfoldAll(PyObject* self, PyObject* args)
{
    SWF swf;
/*    TagListObject*taglist = (TagListObject*)self;
    if(!self || !PyArg_ParseTuple(args,"")) 
	return NULL;
    swf.firstTag = taglist->firstTag;
    swf_UnFoldAll(&swf);
    taglist->firstTag = swf.firstTag;
    taglist->lastTag = 0; // FIXME
    taglist->searchTag = 0;*/
    return PY_NONE;
}
//----------------------------------------------------------------------------
static PyObject * taglist_optimizeOrder(PyObject* self, PyObject* args)
{
    SWF swf;
/*    TagListObject*taglist = (TagListObject*)self;
    if(!self || !PyArg_ParseTuple(args,"")) 
	return NULL;
    swf.firstTag = taglist->firstTag;
    swf_UnFoldAll(&swf);
    taglist->firstTag = swf.firstTag;
    taglist->lastTag = 0; // FIXME
    taglist->searchTag = 0;*/
    return PY_NONE;
}
//----------------------------------------------------------------------------
static void taglist_dealloc(PyObject* self)
{
    TagListObject*taglist = (TagListObject*)self;
    mylog("-%08x(%d) taglist_dealloc\n", (int)self, self->ob_refcnt);
    Py_DECREF(taglist->taglist);
    taglist->taglist = 0;
    Py_DECREF(taglist->tagmap);
    taglist->tagmap= 0;
    PyObject_Del(self);
}
//----------------------------------------------------------------------------
static PyMethodDef taglist_functions[] =
{{"foldAll", taglist_foldAll, METH_VARARGS, "fold all sprites (movieclips) in the list"},
 {"unfoldAll", taglist_unfoldAll, METH_VARARGS, "unfold (expand) all sprites (movieclips) in the list"},
 {"optimizeOrder", taglist_optimizeOrder, METH_VARARGS, "Reorder the Tag structure"},
 {NULL, NULL, 0, NULL}
};

static PyObject* taglist_getattr(PyObject * self, char* a)
{
    PyObject* ret = Py_FindMethod(taglist_functions, self, a);
    mylog(" %08x(%d) taglist_getattr %s: %08x\n", (int)self, self->ob_refcnt, a, ret);
    return ret;
}
//----------------------------------------------------------------------------
static int taglist_length(PyObject * self)
{
    TagListObject*tags = (TagListObject*)self;
    mylog(" %08x(%d) taglist_length", (int)self, self->ob_refcnt);
    return PyList_GET_SIZE(tags->taglist);
}
//----------------------------------------------------------------------------
static int taglist_contains(PyObject * self, PyObject * tag)
{
    mylog(" %08x(%d) taglist_contains %08x", (int)self, self->ob_refcnt, tag);
    TagListObject*taglist = (TagListObject*)self;
    PyObject*list = taglist->taglist;
    int l = PyList_Size(list);
    int t;
    for(t=0;t<l;t++) {
	PyObject*item = PyList_GetItem(list, t);
	if(item == tag) {
	    mylog(" %08x(%d) taglist_contains: yes", (int)self, self->ob_refcnt);
	    return 1;
	}
    }
    mylog(" %08x(%d) taglist_contains: no", (int)self, self->ob_refcnt);
    return 0;
}
//----------------------------------------------------------------------------
static PyObject * taglist_concat(PyObject * self, PyObject* list)
{
    PyObject*tag = 0;
    PY_ASSERT_TYPE(self, &TagListClass);
    TagListObject*taglist = (TagListObject*)self;
    mylog(" %08x(%d) taglist_concat %08x", (int)self, self->ob_refcnt, list);

    if (PyArg_Parse(list, "O!", &TagClass, &tag)) {
	mylog(" %08x(%d) taglist_concat: Tag %08x", (int)self, self->ob_refcnt, tag);
	list = tag_getDependencies(tag);
	int l = PyList_Size(list);
	int t;
	mylog(" %08x(%d) taglist_concat: Tag: %d dependencies", (int)self, self->ob_refcnt, l);
	for(t=0;t<l;t++) {
	    PyObject*item = PyList_GetItem(list, t);
	    PyObject*_self = taglist_concat(self, item);
	    Py_DECREF(self);
	    self = _self;
	}
	if(!taglist_contains(self, tag)) {
	    mylog(" %08x(%d) taglist_concat: Adding Tag %08x", (int)self, self->ob_refcnt, tag);
	    PyList_Append(taglist->taglist, tag);
	}
	mylog(" %08x(%d) taglist_concat: done", (int)self, self->ob_refcnt);
	Py_INCREF(self);
	return self;
	/* copy tag, so we don't have to do INCREF(tag) (and don't
	   get problems if the tag is appended to more than one
	   taglist) */
	/* TODO: handle IDs */
	/*	
	TAG*t = tag_getTAG(tag);
	TAG*nt = 0;
	mylog("taglist_concat: Tag", (int)self, self->ob_refcnt);
	// copy tag
	nt = swf_InsertTag(0, t->id);
	swf_SetBlock(nt,t->data,t->len);
	PyObject*newtag = tag_new(taglist->swf, nt);
	if(swf_isDefiningTag(t)) {
	    int id = swf_GetDefineID(t);
	    PyObject*id = PyLong_FromLong(id);
	    PyDict_SetItem((PyObject*)(taglist->char2id), list, id);
	    Py_INCREF(id);
	    PyDict_SetItem((PyObject*)(taglist->id2char), id, list);
	    Py_INCREF(id);
	}
	Py_INCREF(self);
	return self;*/
    }
    PyErr_Clear();
    if (PyList_Check(list)) {
	int l = PyList_Size(list);
	int t;
	mylog(" %08x(%d) taglist_concat: List", (int)self, self->ob_refcnt);
	for(t=0;t<l;t++) {
	    PyObject*item = PyList_GetItem(list, t);
	    if(!PY_CHECK_TYPE(item, &TagClass)) {
		PyErr_SetString(PyExc_Exception, setError("taglist concatenation only works with tags and lists (%08x).", list));
		return 0;
	    }
	    PyObject*_self = taglist_concat(self, item);
	    Py_DECREF(self);
	    self = _self;
	    if(!self)
		return 0;
	}
	Py_INCREF(self);
	return self;
    }
    PyErr_Clear();
    if (PY_CHECK_TYPE(list, &TagListClass)) {
	mylog(" %08x(%d) taglist_concat: TagList", (int)self, self->ob_refcnt);
	TagListObject*taglist2 = (TagListObject*)list;
	return taglist_concat(self, taglist2->taglist);

	/*TAG* tags = taglist_getTAGs(self);
	TAG* tags2 = taglist_getTAGs(list);
	TAG* tags3;
	tags3 = swf_Concatenate(tags,tags2);
	PyObject* newtaglist = taglist_new(tags3);
	swf_FreeTags(tags3);
	Py_INCREF(newtaglist);*/
    }
    PyErr_Clear();

    PyErr_SetString(PyExc_Exception, setError("taglist concatenation only works with tags and lists (%08x).", list));
    return 0;
}
//----------------------------------------------------------------------------
static PyObject * taglist_item(PyObject * self, int index)
{
    TagListObject*taglist = (TagListObject*)self;
    PyObject*tag;
    mylog(" %08x(%d) taglist_item(%d)", (int)self, self->ob_refcnt, index);
    tag = PyList_GetItem(taglist->taglist, index);
    Py_INCREF(tag); //TODO-REF
    return tag;
}
static PySequenceMethods taglist_as_sequence =
{
    sq_length: taglist_length, // len(obj)
    sq_concat: taglist_concat, // obj += [...], obj1+obj2
    sq_repeat: 0,            // x*n, intargfunc
    sq_item: taglist_item,  // obj[3]
    sq_slice: 0,             // x[i:j] intintargfunc
    sq_ass_item: 0,          // x[i] = y intobjargproc
    sq_ass_slice: 0,         // x[i:j] = v intintobjargproc
    sq_contains: taglist_contains,   //???
};
static PyTypeObject TagListClass = 
{
    PyObject_HEAD_INIT(NULL)
    0,
    tp_name: "TagList",
    tp_basicsize: sizeof(TagListObject),
    tp_itemsize: 0,
    tp_dealloc: taglist_dealloc,
    tp_print: 0,                 // print x
    tp_getattr: taglist_getattr, // x.attr
    tp_setattr: 0,               // x.attr = v
    tp_compare: 0,               // x>y
    tp_repr: 0,                  // `x`, print x
    tp_as_number: 0,
    tp_as_sequence: &taglist_as_sequence,
};
