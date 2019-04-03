//
// Created by jan svejda on 3.4.19.
//

#include "witness.h"

xmlDocPtr parseGraphmlWitness(const char *filename) {
    if (NULL == filename) {
        fprintf(stderr, "No witness file name provided.\n");
        return NULL;
    }
    xmlDocPtr doc;
    xmlNodePtr cur;

    doc = xmlParseFile(filename);

    if (doc == NULL) {
        fprintf(stderr, "Failed to parse witness file.\n");
        return NULL;
    }

    cur = xmlDocGetRootElement(doc);

    if (cur == NULL) {
        fprintf(stderr, "Witness file is empty.\n");
        xmlFreeDoc(doc);
        return NULL;
    }

    if (xmlStrcmp(cur->name, (const xmlChar *) "graphml")) {
        fprintf(stderr, "Witness isn't a GraphML document. Root element=%s", cur->name);
        xmlFreeDoc(doc);
        return NULL;
    }
    return doc;
}
