/* 
  SZARP: SCADA software 

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
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/
/*
 * IPK
 *
 * Pawe� Pa�ucha <pawel@praterm.com.pl>
 *
 * IPK 'unit' class implementation.
 *
 * $Id$
 */

#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <string>
#include <sstream>
#include <dirent.h>
#include <assert.h>

#include "szarp_config.h"
#include "parcook_cfg.h"
#include "conversion.h"
#include "line_cfg.h"
#include "ptt_act.h"
#include "sender_cfg.h"
#include "raporter.h"
#include "ekrncor.h"
#include "liblog.h"

using namespace std;

#define FREE(x)	if (x != NULL) free(x)

int TUnit::parseXML(xmlTextReaderPtr reader)
{

	TParam* p = NULL;
	TSendParam *sp = NULL;

	XMLWrapper xw(reader);

	const char* need_attr[] = { "id", "type", "subtype", "bufsize", 0 };
	if (!xw.AreValidAttr(need_attr)) {
		throw XMLWrapperException();
	}

	const char* ignored_trees[] = { "rate:period", 0 };
	xw.SetIgnoredTrees(ignored_trees);

	for (bool isAttr = xw.IsFirstAttr(); isAttr == true; isAttr = xw.IsNextAttr()) {
		const xmlChar *attr = xw.GetAttr();
		try {
			if (xw.IsAttr("id")) {
				if (xmlStrlen(attr) != 1) {
					xw.XMLError("attribute 'id' should be one ASCII");
				}
				id = SC::U2S(attr)[0];
			} else
			if (xw.IsAttr("type")) {
				type = boost::lexical_cast<int>(attr);
			} else
			if (xw.IsAttr("subtype")) {
				subtype = boost::lexical_cast<int>(attr);
			} else
			if (xw.IsAttr("bufsize")) {
				bufsize = boost::lexical_cast<int>(attr);
			} else
			if (xw.IsAttr("name")) {
				TUnit::name = SC::U2S(attr);
			} else {
				xw.XMLWarningNotKnownAttr();
			}
		} catch (boost::bad_lexical_cast &)  {
			xw.XMLErrorWrongAttrValue();
		}
	}

	assert (params == NULL);
	assert (sendParams == NULL);

	if (!xw.NextTag())
		return 1;

	for (;;) {
		if (xw.IsTag("param")) {
			if (xw.IsBeginTag() || ( xw.IsEndTag() && !xw.IsEmptyTag()) ) {
				if (params == NULL)
					p = params = new TParam(this);
				else
					p = p->Append(new TParam(this));
				if (p->parseXML(reader))
					return 1;
			}
		} else
		if (xw.IsTag("unit")) {
			break;
		} else
		if (xw.IsTag("send")) {
			if (xw.IsBeginTag()) {
				if (sendParams == NULL)
					sp = sendParams = new TSendParam(this);
				else
					sp = sp->Append(new TSendParam(this));
				if (sp->parseXML(reader))
					return 1;
			}
		}
		else {
			xw.XMLErrorNotKnownTag("unit");
		}
		if (!xw.NextTag())
			return 1;
	}

	return 0;
}

int TUnit::parseXML(xmlNodePtr node)
{
	unsigned char* c = NULL;
	xmlNodePtr ch;
	
#define NOATR(p, n) \
	{ \
		sz_log(1, "XML parsing error: attribute '%s' in node '%s' not\
 found (line %ld)", \
 			n, SC::U2A(p->name).c_str(), xmlGetLineNo(p)); \
			return 1; \
	}
#define NEEDATR(p, n) \
	if (c) free(c); \
	c = xmlGetNoNsProp(p, (xmlChar *)n); \
	if (!c) NOATR(p, n);
#define X (xmlChar *)
	NEEDATR(node, "id");
	if (xmlStrlen(c) != 1) {
		sz_log(1, "XML file error: attribute 'id' should be one ASCII\
 character (line %ld)", 
 			xmlGetLineNo(node));
		return 1;
	}
	id = SC::U2S(c)[0];
	NEEDATR(node, "type");
	type = atoi((char*)c);
	NEEDATR(node, "subtype");
	subtype = atoi((char*)c);
	NEEDATR(node, "bufsize");
	bufsize = atoi((char*)c);
	free(c);
	c = xmlGetNoNsProp(node, (xmlChar *) "name");
	if (c) {
		name = SC::U2S(c);
		free(c);
	}
	
	assert (params == NULL);
	assert (sendParams == NULL);
	TParam* p = NULL;
	TSendParam* sp = NULL;
	for (ch = node->children; ch; ch = ch->next) {
		if (!strcmp((char *)ch->name, "param")) {
			if (params == NULL)
				p = params = new TParam(this);
			else
				p = p->Append(new TParam(this));
			if (p->parseXML(ch))
				return 1;
		} else if (!strcmp((char *)ch->name, "send")) {
			if (sendParams == NULL)
				sp = sendParams = new TSendParam(this);
			else
				sp = sp->Append(new TSendParam(this));
			if (sp->parseXML(ch))
				return 1;
		}
	}
	return 0;
#undef NEEDATR
#undef NOATR
#undef X
}

xmlNodePtr TUnit::generateXMLNode(void)
{
#define X (unsigned char *)
#define ITOA(x) snprintf(buffer, 10, "%d", x)
#define BUF X(buffer)
	char buffer[10];
	xmlNodePtr r;

	std::wstringstream wss;
	wss << id;

	r = xmlNewNode(NULL, X"unit");
	sprintf(buffer, "%s", SC::S2U(wss.str()).c_str());
	xmlSetProp(r, X"id", BUF);
	ITOA(type);
	xmlSetProp(r, X"type", BUF);
	ITOA(subtype);
	xmlSetProp(r, X"subtype", BUF);
	ITOA(bufsize);
	xmlSetProp(r, X"bufsize", BUF);

	if (!name.empty())
		xmlSetProp(r, X"name", SC::S2U(name).c_str());

	for (TParam* p = params; p; p = p->GetNext()) {
		xmlAddChild(r, p->generateXMLNode());
	}
	for (TSendParam* sp = GetFirstSendParam(); sp; sp =
			GetNextSendParam(sp))
		xmlAddChild(r, sp->generateXMLNode());
	return r;
#undef X
#undef ITOA
#undef BUF
}

TDevice* TUnit::GetDevice() const
{
	return parentRadio->GetDevice();
}

TSzarpConfig* TUnit::GetSzarpConfig() const
{
	return GetDevice()->GetSzarpConfig();
}

TSendParam* TUnit::GetNextSendParam(TSendParam* s)
{
	if (s == NULL)
		return NULL;
	return s->GetNext();
}

TUnit* TUnit::Append(TUnit* u)
{
	TUnit* t = this;
	while (t->next)
		t = t->next;
	t->next = u;
	return u;
}

int TUnit::GetParamsCount()
{
	int i;
	TParam* p;
	for (i = 0, p = params; p; i++, p = p->GetNext());
	return i;
}

int TUnit::GetSendParamsCount()
{
	int i;
	TSendParam* s;
	for (i = 0, s = sendParams; s; i++, s = s->GetNext());
	return i;
}

void TUnit::AddParam(TParam* p)
{
	if (params == NULL)
		params = p;
	else
		params->Append(p);
}

void TUnit::AddParam(TSendParam* s)
{
	if (sendParams == NULL)
		sendParams = s;
	else
		sendParams->Append(s);
}

TParam* TUnit::GetNextParam(TParam* p)
{
	if (p == NULL)
		return NULL;
	return p->GetNext();
}

const std::wstring& TUnit::GetUnitName() 
{
	return name;
}

TUnit::~TUnit()
{
	delete next;
	delete params;
	delete sendParams;
}

