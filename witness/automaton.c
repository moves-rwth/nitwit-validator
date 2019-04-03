//
// Created by jan svejda on 3.4.19.
//

#include <assert.h>
#include "automaton.h"
#include "../utils/sglib.h"

typedef struct Key {
    xmlChar *name;
    xmlChar *type;
    xmlChar *for_;
    xmlChar *id;
    xmlChar *default_val;
} Key;

void keyFree(Key *pKey) {
    xmlFree(pKey->name);
    xmlFree(pKey->type);
    xmlFree(pKey->for_);
    xmlFree(pKey->id);
//    xmlFree(pKey->default_val);
}

typedef struct keylist {
    Key *value;
    struct keylist *next;
    struct keylist *previous;
} KeyList;

int myListCmp(KeyList *e1, KeyList *e2) {
    return xmlStrcmp(e1->value->name, e2->value->name);
}

SGLIB_DEFINE_DL_LIST_PROTOTYPES(KeyList, myListCmp, previous, next)

SGLIB_DEFINE_DL_LIST_FUNCTIONS(KeyList, myListCmp, previous, next)

DefaultKeyValues *parseKeys(xmlXPathObjectPtr keyNodeSet);


void setAttributes(Key *key, xmlNodePtr node, const xmlAttr *attribute);

void printKey(Key *k);

Key *initKey();

xmlXPathObjectPtr getnodeset(xmlDocPtr doc, xmlChar *xpath) {

    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
        xmlXPathFreeObject(result);
        printf("No result\n");
        return NULL;
    }
    return result;
}

struct Automaton *automatonFromWitness(xmlDocPtr doc) {
    assert(doc != NULL);

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (root == NULL || xmlStrcmp(root->name, (const xmlChar *) "graphml")) {
        fprintf(stderr, "No graphml root element.");
        return NULL;
    }

    xmlChar *xpath = (xmlChar *) "/graphml/key";
    xmlXPathObjectPtr key_result = getnodeset(doc, xpath);
    if (key_result == NULL) {
        fprintf(stderr, "No graphml keys found!");
        return NULL;
    }
    DefaultKeyValues *default_key_values = parseKeys(key_result);

    return NULL; // TODO return true automaton
}

DefaultKeyValues *parseKeys(xmlXPathObjectPtr keyNodeSet) {
    assert(keyNodeSet != NULL);
    xmlNodeSetPtr setPtr = keyNodeSet->nodesetval;
    KeyList *kl, *key_list = NULL;
    for (int i = 0; i < setPtr->nodeNr; ++i) {
        Key *k = initKey();
        xmlNodePtr node = setPtr->nodeTab[i];
        xmlAttr *attribute = node->properties;
        while (attribute) {
            setAttributes(k, node, attribute);
            attribute = attribute->next;
        }

        xmlNodePtr cur = node->children;
        while (cur != NULL) {
            if (xmlStrcmp(cur->name, (const xmlChar *) "default") == 0) {
                // has default value
                k->default_val = xmlNodeGetContent(cur);
                printf("default value %s\n", xmlNodeGetContent(cur));
            }
            cur = cur->next;
        }
        kl = malloc(sizeof *kl);
        kl->value = k;
        sglib_KeyList_add(&key_list, kl);
    }

    struct sglib_KeyList_iterator it;
    for (kl = sglib_KeyList_it_init(&it, key_list); kl != NULL; kl = sglib_KeyList_it_next(&it)) {
        printKey(kl->value);
        keyFree(kl->value);
        free(kl->value);
        free(kl);
    }

    return NULL;
}

Key *initKey() {
    Key *k = malloc(sizeof *k);
    k->name = NULL;
    k->type = NULL;
    k->for_ = NULL;
    k->id = NULL;
    k->default_val = NULL;
    return k;
}

void printKey(Key *k) {
    printf("name: %s\ttype: %s\tfor: %s\tid: %s\tdef: %s\n", k->name, k->type, k->for_, k->id, k->default_val);
}


void setAttributes(Key *key, xmlNodePtr node, const xmlAttr *attribute) {
    xmlChar *value = xmlNodeListGetString(node->doc, attribute->children, 1);
    const xmlChar *attributeName = attribute->name;
//    printf("%s=%s\n", attributeName, value);

    if (xmlStrcmp(attributeName, (const xmlChar *) "attr.name") == 0) {
        key->name = value;
    } else if (xmlStrcmp(attributeName, (const xmlChar *) "attr.type") == 0) {
        key->type = value;
    } else if (xmlStrcmp(attributeName, (const xmlChar *) "for") == 0) {
        key->for_ = value;
    } else if (xmlStrcmp(attributeName, (const xmlChar *) "id") == 0) {
        key->id = value;
    }
}
