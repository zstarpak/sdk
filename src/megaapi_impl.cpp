﻿/**
 * @file megaapi_impl.cpp
 * @brief Private implementation of the intermediate layer for the MEGA C++ SDK.
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#define _LARGE_FILES

#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#define USE_VARARGS
#define PREFER_STDARG
#include "megaapi_impl.h"
#include "megaapi.h"

#include <iomanip>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <cmath>

#ifndef _WIN32
#ifndef _LARGEFILE64_SOURCE
    #define _LARGEFILE64_SOURCE
#endif
#include <signal.h>
#endif


#ifdef __APPLE__
    #include <xlocale.h>
    #include <strings.h>

    #if TARGET_OS_IPHONE
    #include <netdb.h>
    #include <resolv.h>
    #include <arpa/inet.h>
    #endif
#endif

#ifdef _WIN32
#ifndef WINDOWS_PHONE
#include <shlwapi.h>
#endif

#endif

#if (!defined(_WIN32) && !defined(USE_CURL_PUBLIC_KEY_PINNING)) || defined(WINDOWS_PHONE)
#include <openssl/rand.h>
#endif

using namespace mega;

MegaNodePrivate::MegaNodePrivate(const char *name, int type, int64_t size, int64_t ctime, int64_t mtime, uint64_t nodehandle,
                                 string *nodekey, string *attrstring, const char *fingerprint, MegaHandle parentHandle,
                                 const char *privateauth, const char *publicauth, bool ispublic, bool isForeign)
: MegaNode()
{
    this->name = MegaApi::strdup(name);
    this->fingerprint = MegaApi::strdup(fingerprint);
    this->customAttrs = NULL;
    this->duration = -1;
    this->latitude = INVALID_COORDINATE;
    this->longitude = INVALID_COORDINATE;
    this->type = type;
    this->size = size;
    this->ctime = ctime;
    this->mtime = mtime;
    this->nodehandle = nodehandle;
    this->parenthandle = parentHandle;
    this->attrstring.assign(attrstring->data(), attrstring->size());
    this->nodekey.assign(nodekey->data(), nodekey->size());
    this->changed = 0;
    this->thumbnailAvailable = false;
    this->previewAvailable = false;
    this->tag = 0;
    this->isPublicNode = ispublic;
    this->outShares = false;
    this->inShare = false;
    this->plink = NULL;
    this->foreign = isForeign;

    if (privateauth)
    {
        this->privateAuth = privateauth;
    }

    if (publicauth)
    {
        this->publicAuth = publicauth;
    }

#ifdef ENABLE_SYNC
    this->syncdeleted = false;
#endif
}

MegaNodePrivate::MegaNodePrivate(MegaNode *node)
: MegaNode()
{
    this->name = MegaApi::strdup(node->getName());
    this->fingerprint = MegaApi::strdup(node->getFingerprint());
    this->customAttrs = NULL;
    this->duration = node->getDuration();
    this->latitude = node->getLatitude();
    this->longitude = node->getLongitude();
    this->type = node->getType();
    this->size = node->getSize();
    this->ctime = node->getCreationTime();
    this->mtime = node->getModificationTime();
    this->nodehandle = node->getHandle();
    this->parenthandle = node->getParentHandle();
    string * attrstring = node->getAttrString();
    this->attrstring.assign(attrstring->data(), attrstring->size());
    string *nodekey = node->getNodeKey();
    this->nodekey.assign(nodekey->data(),nodekey->size());
    this->changed = node->getChanges();
    this->thumbnailAvailable = node->hasThumbnail();
    this->previewAvailable = node->hasPreview();
    this->tag = node->getTag();
    this->isPublicNode = node->isPublic();
    this->privateAuth = *node->getPrivateAuth();
    this->publicAuth = *node->getPublicAuth();
    this->outShares = node->isOutShare();
    this->inShare = node->isInShare();
    this->foreign = node->isForeign();

    if (node->isExported())
    {
        this->plink = new PublicLink(node->getPublicHandle(), node->getExpirationTime(), node->isTakenDown());
    }
    else
        this->plink = NULL;

    if (node->hasCustomAttrs())
    {
        this->customAttrs = new attr_map();
        MegaStringList *names = node->getCustomAttrNames();
        for (int i = 0; i < names->size(); i++)
        {
            (*customAttrs)[AttrMap::string2nameid(names->get(i))] = node->getCustomAttr(names->get(i));
        }
        delete names;
    }

#ifdef ENABLE_SYNC
    this->syncdeleted = node->isSyncDeleted();
    this->localPath = node->getLocalPath();
#endif
}

MegaNodePrivate::MegaNodePrivate(Node *node)
: MegaNode()
{
    this->name = MegaApi::strdup(node->displayname());
    this->fingerprint = NULL;

    if (node->isvalid)
    {
        string fingerprint;
        node->serializefingerprint(&fingerprint);
        m_off_t size = node->size;
        char bsize[sizeof(size)+1];
        int l = Serialize64::serialize((byte *)bsize, size);
        char *buf = new char[l * 4 / 3 + 4];
        char ssize = 'A' + Base64::btoa((const byte *)bsize, l, buf);
        string result(1, ssize);
        result.append(buf);
        result.append(fingerprint);
        delete [] buf;

        this->fingerprint = MegaApi::strdup(result.c_str());
    }

    this->duration = -1;
    this->latitude = INVALID_COORDINATE;
    this->longitude = INVALID_COORDINATE;
    this->customAttrs = NULL;

    char buf[10];
    for (attr_map::iterator it = node->attrs.map.begin(); it != node->attrs.map.end(); it++)
    {
       buf[0] = 0;
       node->attrs.nameid2string(it->first, buf);
       if (buf[0] == '_')
       {
           if (!customAttrs)
           {
               customAttrs = new attr_map();
           }

           nameid id = AttrMap::string2nameid(&buf[1]);
           (*customAttrs)[id] = it->second;
       }
       else
       {
           if (it->first == AttrMap::string2nameid("d"))
           {
               memcpy(&duration, it->second.data(), sizeof duration);
           }
           else if (it->first == AttrMap::string2nameid("gps"))
           {
               string coords = it->second;
               size_t separator = coords.find_first_of(';');
               if (separator != coords.npos && (separator + 1 < coords.size()))
               {
                   const char *ptr = coords.substr(0, separator).c_str();
                   char *endptr = NULL;
                   latitude = std::strtod(ptr, &endptr);
                   if ((latitude == 0 && endptr == ptr) || (latitude == HUGE_VAL))
                   {
                       latitude = INVALID_COORDINATE;
                   }

                   ptr = coords.substr(separator+1, coords.length()).c_str();
                   endptr = NULL;
                   longitude = std::strtod(ptr, &endptr);
                   if ((longitude == 0 && endptr == ptr) || (longitude == HUGE_VAL))
                   {
                       longitude = INVALID_COORDINATE;
                   }
               }
           }
       }
    }

    this->type = node->type;
    this->size = node->size;
    this->ctime = node->ctime;
    this->mtime = node->mtime;
    this->nodehandle = node->nodehandle;
    this->parenthandle = node->parent ? node->parent->nodehandle : INVALID_HANDLE;

    if(node->attrstring)
    {
        this->attrstring.assign(node->attrstring->data(), node->attrstring->size());
    }
    this->nodekey.assign(node->nodekey.data(),node->nodekey.size());

    this->changed = 0;
    if(node->changed.attrs)
    {
        this->changed |= MegaNode::CHANGE_TYPE_ATTRIBUTES;
    }
    if(node->changed.ctime)
    {
        this->changed |= MegaNode::CHANGE_TYPE_TIMESTAMP;
    }
    if(node->changed.fileattrstring)
    {
        this->changed |= MegaNode::CHANGE_TYPE_FILE_ATTRIBUTES;
    }
    if(node->changed.inshare)
    {
        this->changed |= MegaNode::CHANGE_TYPE_INSHARE;
    }
    if(node->changed.outshares)
    {
        this->changed |= MegaNode::CHANGE_TYPE_OUTSHARE;
    }
    if(node->changed.pendingshares)
    {
        this->changed |= MegaNode::CHANGE_TYPE_PENDINGSHARE;
    }
    if(node->changed.owner)
    {
        this->changed |= MegaNode::CHANGE_TYPE_OWNER;
    }
    if(node->changed.parent)
    {
        this->changed |= MegaNode::CHANGE_TYPE_PARENT;
    }
    if(node->changed.removed)
    {
        this->changed |= MegaNode::CHANGE_TYPE_REMOVED;
    }
    if(node->changed.publiclink)
    {
        this->changed |= MegaNode::CHANGE_TYPE_PUBLIC_LINK;
    }


#ifdef ENABLE_SYNC
	this->syncdeleted = (node->syncdeleted != SYNCDEL_NONE);
    if(node->localnode)
    {
        node->localnode->getlocalpath(&localPath, true);
        localPath.append("", 1);
    }
#endif

    this->thumbnailAvailable = (node->hasfileattribute(0) != 0);
    this->previewAvailable = (node->hasfileattribute(1) != 0);
    this->tag = node->tag;
    this->isPublicNode = false;
    this->foreign = false;

    // if there's only one share and it has no user --> public link
    this->outShares = (node->outshares) ? (node->outshares->size() > 1 || node->outshares->begin()->second->user) : false;
    this->inShare = (node->inshare != NULL) && !node->parent;
    this->plink = node->plink ? new PublicLink(node->plink) : NULL;
}

MegaNode *MegaNodePrivate::copy()
{
    return new MegaNodePrivate(this);
}

bool MegaNodePrivate::serialize(string *d)
{
    unsigned short ll;
    bool flag;

    ll = name ? strlen(name) + 1 : 0;
    d->append((char*)&ll, sizeof(ll));
    d->append(name, ll);

    ll = fingerprint ? strlen(fingerprint) + 1 : 0;
    d->append((char*)&ll, sizeof(ll));
    d->append(fingerprint, ll);

    d->append((char*)&size, sizeof(size));
    d->append((char*)&ctime, sizeof(ctime));
    d->append((char*)&mtime, sizeof(mtime));
    d->append((char*)&nodehandle, sizeof(nodehandle));
    d->append((char*)&parenthandle, sizeof(parenthandle));

    ll = (unsigned short)attrstring.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(attrstring.data(), ll);

    ll = (unsigned short)nodekey.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(nodekey.data(), ll);

    ll = (unsigned short)privateAuth.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(privateAuth.data(), ll);

    ll = (unsigned short)publicAuth.size();
    d->append((char*)&ll, sizeof(ll));
    d->append(publicAuth.data(), ll);

    flag = isPublicNode;
    d->append((char*)&flag, sizeof(flag));

    flag = foreign;
    d->append((char*)&flag, sizeof(flag));

    d->append("\0\0\0\0\0\0\0", 8);

    return true;
}

MegaNodePrivate *MegaNodePrivate::unserialize(string *d)
{
    const char* ptr = d->data();
    const char* end = ptr + d->size();

    if (ptr + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaNode unserialization failed - data too short";
        return NULL;
    }

    unsigned short namelen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(namelen);
    if (ptr + namelen + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaNode unserialization failed - name too long";
        return NULL;
    }
    string name;
    if (namelen)
    {
        name.assign(ptr, namelen - 1);
    }
    ptr += namelen;

    unsigned short fingerprintlen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(fingerprintlen);
    if (ptr + fingerprintlen + sizeof(unsigned short)
            + sizeof(int64_t) + sizeof(int64_t)
            + sizeof(int64_t) + sizeof(MegaHandle)
            + sizeof(MegaHandle) + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaNode unserialization failed - fingerprint too long";
        return NULL;
    }
    string fingerprint;
    if (fingerprintlen)
    {
        fingerprint.assign(ptr, fingerprintlen - 1);
    }
    ptr += fingerprintlen;

    int64_t size = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(int64_t);

    int64_t ctime = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(int64_t);

    int64_t mtime = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(int64_t);

    MegaHandle nodehandle = MemAccess::get<MegaHandle>(ptr);
    ptr += sizeof(MegaHandle);

    MegaHandle parenthandle = MemAccess::get<MegaHandle>(ptr);
    ptr += sizeof(MegaHandle);

    unsigned short ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(ll);
    if (ptr + ll + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaNode unserialization failed - attrstring too long";
        return NULL;
    }
    string attrstring;
    attrstring.assign(ptr, ll);
    ptr += ll;

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(ll);
    if (ptr + ll + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaNode unserialization failed - nodekey too long";
        return NULL;
    }
    string nodekey;
    nodekey.assign(ptr, ll);
    ptr += ll;

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(ll);
    if (ptr + ll + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaNode unserialization failed - auth too long";
        return NULL;
    }
    string privauth;
    privauth.assign(ptr, ll);
    ptr += ll;

    ll = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(ll);
    if (ptr + ll + sizeof(bool) + sizeof(bool) + 8 > end)
    {
        LOG_err << "MegaNode unserialization failed - auth too long";
        return NULL;
    }

    string pubauth;
    privauth.assign(ptr, ll);
    ptr += ll;

    bool isPublicNode = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    bool foreign = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    if (memcmp(ptr, "\0\0\0\0\0\0\0", 8))
    {
        LOG_err << "MegaNodePrivate unserialization failed - invalid version";
        return NULL;
    }
    ptr += 8;

    d->erase(0, ptr - d->data());

    return new MegaNodePrivate(namelen ? name.c_str() : NULL, FILENODE, size, ctime,
                               mtime, nodehandle, &nodekey, &attrstring,
                               fingerprintlen ? fingerprint.c_str() : NULL,
                               parenthandle, privauth.c_str(), pubauth.c_str(),
                               isPublicNode, foreign);
}

char *MegaNodePrivate::getBase64Handle()
{
    char *base64Handle = new char[12];
    Base64::btoa((byte*)&(nodehandle),MegaClient::NODEHANDLE,base64Handle);
    return base64Handle;
}

int MegaNodePrivate::getType()
{
	return type;
}

const char* MegaNodePrivate::getName()
{
    if(type <= FOLDERNODE)
    {
        return name;
    }

    switch(type)
    {
        case ROOTNODE:
            return "Cloud Drive";
        case INCOMINGNODE:
            return "Inbox";
        case RUBBISHNODE:
            return "Rubbish Bin";
        default:
            return name;
    }
}

const char *MegaNodePrivate::getFingerprint()
{
    return fingerprint;
}

bool MegaNodePrivate::hasCustomAttrs()
{
    return customAttrs != NULL;
}

MegaStringList *MegaNodePrivate::getCustomAttrNames()
{
    if (!customAttrs)
    {
        return new MegaStringList();
    }

    vector<char*> names;
    char *buf;
    for (attr_map::iterator it = customAttrs->begin(); it != customAttrs->end(); it++)
    {
        buf = new char[10];
        AttrMap::nameid2string(it->first, buf);
        names.push_back(buf);
    }
    return new MegaStringListPrivate(names.data(), names.size());
}

const char *MegaNodePrivate::getCustomAttr(const char *attrName)
{
    if (!customAttrs)
    {
        return NULL;
    }

    nameid n = AttrMap::string2nameid(attrName);
    if (!n)
    {
        return NULL;
    }

    attr_map::iterator it = customAttrs->find(n);
    if (it == customAttrs->end())
    {
        return NULL;
    }

    return it->second.c_str();
}

int MegaNodePrivate::getDuration()
{
    return duration;
}

double MegaNodePrivate::getLatitude()
{
    return latitude;
}

double MegaNodePrivate::getLongitude()
{
    return longitude;
}

int64_t MegaNodePrivate::getSize()
{
	return size;
}

int64_t MegaNodePrivate::getCreationTime()
{
	return ctime;
}

int64_t MegaNodePrivate::getModificationTime()
{
    return mtime;
}

MegaHandle MegaNodePrivate::getParentHandle()
{
    return parenthandle;
}

uint64_t MegaNodePrivate::getHandle()
{
	return nodehandle;
}

string *MegaNodePrivate::getNodeKey()
{
    return &nodekey;
}

char *MegaNodePrivate::getBase64Key()
{
    char *key = NULL;

    // the key
    if (type == FILENODE && nodekey.size() >= FILENODEKEYLENGTH)
    {
        key = new char[FILENODEKEYLENGTH*4/3+3];
        Base64::btoa((const byte*)nodekey.data(),FILENODEKEYLENGTH, key);
    }

    return key;
}

string *MegaNodePrivate::getAttrString()
{
	return &attrstring;
}

int MegaNodePrivate::getTag()
{
    return tag;
}

int64_t MegaNodePrivate::getExpirationTime()
{
    return plink ? plink->ets : -1;
}

MegaHandle MegaNodePrivate::getPublicHandle()
{
    return plink ? (MegaHandle) plink->ph : INVALID_HANDLE;
}

MegaNode* MegaNodePrivate::getPublicNode()
{
    if (!plink || plink->isExpired())
    {
        return NULL;
    }

    char *skey = getBase64Key();
    string key(skey);

    MegaNode *node = new MegaNodePrivate(
                name, type, size, ctime, mtime,
                plink->ph, &key, &attrstring, fingerprint,
                INVALID_HANDLE);

    delete [] skey;

    return node;
}

char *MegaNodePrivate::getPublicLink()
{
    if (!plink)
    {
        return NULL;
    }

    char *base64ph = new char[12];
    Base64::btoa((byte*)&(plink->ph), MegaClient::NODEHANDLE, base64ph);

    char *base64k = getBase64Key();

    string strlink = "https://mega.nz/#";
    strlink += (type ? "F" : "");
    strlink += "!";
    strlink += base64ph;
    strlink += "!";
    strlink += base64k;

    char *link = MegaApi::strdup(strlink.c_str());

    delete [] base64ph;
    delete [] base64k;

    return link;
}

bool MegaNodePrivate::isFile()
{
	return type == TYPE_FILE;
}

bool MegaNodePrivate::isFolder()
{
    return (type != TYPE_FILE) && (type != TYPE_UNKNOWN);
}

bool MegaNodePrivate::isRemoved()
{
    return hasChanged(MegaNode::CHANGE_TYPE_REMOVED);
}

bool MegaNodePrivate::hasChanged(int changeType)
{
    return (changed & changeType);
}

int MegaNodePrivate::getChanges()
{
    return changed;
}


const unsigned int MegaApiImpl::MAX_SESSION_LENGTH = 64;

#ifdef ENABLE_SYNC
bool MegaNodePrivate::isSyncDeleted()
{
    return syncdeleted;
}

string MegaNodePrivate::getLocalPath()
{
    return localPath;
}

bool WildcardMatch(const char *pszString, const char *pszMatch)
//  cf. http://www.planet-source-code.com/vb/scripts/ShowCode.asp?txtCodeId=1680&lngWId=3
{
    const char *cp;
    const char *mp;

    while ((*pszString) && (*pszMatch != '*'))
    {
        if ((*pszMatch != *pszString) && (*pszMatch != '?'))
        {
            return false;
        }
        pszMatch++;
        pszString++;
    }

    while (*pszString)
    {
        if (*pszMatch == '*')
        {
            if (!*++pszMatch)
            {
                return true;
            }
            mp = pszMatch;
            cp = pszString + 1;
        }
        else if ((*pszMatch == *pszString) || (*pszMatch == '?'))
        {
            pszMatch++;
            pszString++;
        }
        else
        {
            pszMatch = mp;
            pszString = cp++;
        }
    }
    while (*pszMatch == '*')
    {
        pszMatch++;
    }
    return !*pszMatch;
}

bool MegaApiImpl::is_syncable(const char *name)
{
    for(unsigned int i=0; i< excludedNames.size(); i++)
    {
        if(WildcardMatch(name, excludedNames[i].c_str()))
        {
            return false;
        }
    }

    return true;
}

bool MegaApiImpl::is_syncable(long long size)
{
    if (!syncLowerSizeLimit)
    {
        // No lower limit. Check upper limit only
        if (syncUpperSizeLimit && size > syncUpperSizeLimit)
        {
            return false;
        }
    }
    else if (!syncUpperSizeLimit)
    {
        // No upper limit. Check lower limit only
        if (syncLowerSizeLimit && size < syncLowerSizeLimit)
        {
            return false;
        }
    }
    else
    {
        //Upper and lower limit
        if(syncLowerSizeLimit < syncUpperSizeLimit)
        {
            // Normal use case:
            // Exclude files with a size lower than the lower limit
            // or greater than the upper limit
            if(size < syncLowerSizeLimit || size > syncUpperSizeLimit)
            {
                return false;
            }
        }
        else
        {
            // Special use case:
            // Exclude files with a size lower than the lower limit
            // AND greater than the upper limit
            if(size < syncLowerSizeLimit && size > syncUpperSizeLimit)
            {
                return false;
            }
        }
    }

    return true;
}

bool MegaApiImpl::isIndexing()
{
    if(!client || client->syncs.size() == 0)
    {
        return false;
    }

    if(client->syncscanstate)
    {
        return true;
    }

    bool indexing = false;
    sdkMutex.lock();
    sync_list::iterator it = client->syncs.begin();
    while(it != client->syncs.end())
    {
        Sync *sync = (*it);
        if(sync->state == SYNC_INITIALSCAN)
        {
            indexing = true;
            break;
        }
        it++;
    }
    sdkMutex.unlock();
    return indexing;
}
#endif

bool MegaNodePrivate::hasThumbnail()
{
	return thumbnailAvailable;
}

bool MegaNodePrivate::hasPreview()
{
    return previewAvailable;
}

bool MegaNodePrivate::isPublic()
{
    return isPublicNode;
}

bool MegaNodePrivate::isShared()
{
    return outShares || inShare;
}

bool MegaNodePrivate::isOutShare()
{
    return outShares;
}

bool MegaNodePrivate::isInShare()
{
    return inShare;
}

bool MegaNodePrivate::isExported()
{
    return plink;
}

bool MegaNodePrivate::isExpired()
{
    return plink ? (plink->isExpired()) : false;
}

bool MegaNodePrivate::isTakenDown()
{
    return plink ? plink->takendown : false;
}

bool MegaNodePrivate::isForeign()
{
    return foreign;
}

string *MegaNodePrivate::getPrivateAuth()
{
    return &privateAuth;
}

void MegaNodePrivate::setPrivateAuth(const char *privateAuth)
{
    if (!privateAuth || !privateAuth[0])
    {
        this->privateAuth.clear();
    }
    else
    {
        this->privateAuth = privateAuth;
    }
}

void MegaNodePrivate::setPublicAuth(const char *publicAuth)
{
    if (!publicAuth || !publicAuth[0])
    {
        this->publicAuth.clear();
    }
    else
    {
        this->publicAuth = publicAuth;
    }
}

void MegaNodePrivate::setForeign(bool foreign)
{
    this->foreign = foreign;
}

string *MegaNodePrivate::getPublicAuth()
{
    return &publicAuth;
}

MegaNodePrivate::~MegaNodePrivate()
{
    delete[] name;
    delete [] fingerprint;
    delete customAttrs;
    delete plink;
}

MegaUserPrivate::MegaUserPrivate(User *user) : MegaUser()
{
    email = MegaApi::strdup(user->email.c_str());
    handle = user->userhandle;
	visibility = user->show;
	ctime = user->ctime;
    changed = 0;
    if (user->changed.auth)
    {
        changed |= MegaUser::CHANGE_TYPE_AUTH;
    }
    if(user->changed.avatar)
    {
        changed |= MegaUser::CHANGE_TYPE_AVATAR;
    }
    if(user->changed.lstint)
    {
        changed |= MegaUser::CHANGE_TYPE_LSTINT;
    }
    if(user->changed.firstname)
    {
        changed |= MegaUser::CHANGE_TYPE_FIRSTNAME;
    }
    if(user->changed.lastname)
    {
        changed |= MegaUser::CHANGE_TYPE_LASTNAME;
    }
    if(user->changed.email)
    {
        changed |= MegaUser::CHANGE_TYPE_EMAIL;
    }
}

MegaUserPrivate::MegaUserPrivate(MegaUser *user) : MegaUser()
{
	email = MegaApi::strdup(user->getEmail());
    handle = user->getHandle();
	visibility = user->getVisibility();
	ctime = user->getTimestamp();
    changed = user->getChanges();
}

MegaUser *MegaUserPrivate::fromUser(User *user)
{
    if(!user)
    {
        return NULL;
    }
    return new MegaUserPrivate(user);
}

MegaUser *MegaUserPrivate::copy()
{
	return new MegaUserPrivate(this);
}

MegaUserPrivate::~MegaUserPrivate()
{
	delete[] email;
}

const char* MegaUserPrivate::getEmail()
{
	return email;
}

MegaHandle MegaUserPrivate::getHandle()
{
    return handle;
}

int MegaUserPrivate::getVisibility()
{
	return visibility;
}

int64_t MegaUserPrivate::getTimestamp()
{
	return ctime;
}

bool MegaUserPrivate::hasChanged(int changeType)
{
    return (changed & changeType);
}

int MegaUserPrivate::getChanges()
{
    return changed;
}


MegaNode *MegaNodePrivate::fromNode(Node *node)
{
    if(!node) return NULL;
    return new MegaNodePrivate(node);
}

MegaSharePrivate::MegaSharePrivate(MegaShare *share) : MegaShare()
{
	this->nodehandle = share->getNodeHandle();
	this->user = MegaApi::strdup(share->getUser());
	this->access = share->getAccess();
	this->ts = share->getTimestamp();
}

MegaShare *MegaSharePrivate::copy()
{
	return new MegaSharePrivate(this);
}

MegaSharePrivate::MegaSharePrivate(uint64_t handle, Share *share)
{
    this->nodehandle = handle;
    this->user = share->user ? MegaApi::strdup(share->user->email.c_str()) : NULL;
	this->access = share->access;
	this->ts = share->ts;
}

MegaShare *MegaSharePrivate::fromShare(uint64_t nodeuint64_t, Share *share)
{
    return new MegaSharePrivate(nodeuint64_t, share);
}

MegaSharePrivate::~MegaSharePrivate()
{
	delete[] user;
}

const char *MegaSharePrivate::getUser()
{
	return user;
}

uint64_t MegaSharePrivate::getNodeHandle()
{
    return nodehandle;
}

int MegaSharePrivate::getAccess()
{
	return access;
}

int64_t MegaSharePrivate::getTimestamp()
{
	return ts;
}


MegaTransferPrivate::MegaTransferPrivate(int type, MegaTransferListener *listener)
{
    this->type = type;
    this->tag = -1;
    this->path = NULL;
    this->nodeHandle = UNDEF;
    this->parentHandle = UNDEF;
    this->startPos = -1;
    this->endPos = -1;
    this->parentPath = NULL;
    this->listener = listener;
    this->retry = 0;
    this->maxRetries = 7;
    this->time = -1;
    this->startTime = 0;
    this->transferredBytes = 0;
    this->totalBytes = 0;
    this->fileName = NULL;
    this->transfer = NULL;
    this->speed = 0;
    this->deltaSize = 0;
    this->updateTime = 0;
    this->publicNode = NULL;
    this->lastBytes = NULL;
    this->syncTransfer = false;
    this->streamingTransfer = false;
    this->lastError = API_OK;
    this->folderTransferTag = 0;
    this->appData = NULL;
}

MegaTransferPrivate::MegaTransferPrivate(const MegaTransferPrivate *transfer)
{
    path = NULL;
    parentPath = NULL;
    fileName = NULL;
    publicNode = NULL;
    lastBytes = NULL;
    appData = NULL;

    this->listener = transfer->getListener();
    this->transfer = transfer->getTransfer();
    this->type = transfer->getType();
    this->setTag(transfer->getTag());
    this->setPath(transfer->getPath());
    this->setNodeHandle(transfer->getNodeHandle());
    this->setParentHandle(transfer->getParentHandle());
    this->setStartPos(transfer->getStartPos());
    this->setEndPos(transfer->getEndPos());
    this->setParentPath(transfer->getParentPath());
    this->setNumRetry(transfer->getNumRetry());
    this->setMaxRetries(transfer->getMaxRetries());
    this->setTime(transfer->getTime());
    this->setStartTime(transfer->getStartTime());
    this->setTransferredBytes(transfer->getTransferredBytes());
    this->setTotalBytes(transfer->getTotalBytes());
    this->setFileName(transfer->getFileName());
    this->setSpeed(transfer->getSpeed());
    this->setDeltaSize(transfer->getDeltaSize());
    this->setUpdateTime(transfer->getUpdateTime());
    this->setPublicNode(transfer->getPublicNode());
    this->setTransfer(transfer->getTransfer());
    this->setSyncTransfer(transfer->isSyncTransfer());
    this->setStreamingTransfer(transfer->isStreamingTransfer());
    this->setLastError(transfer->getLastError());
    this->setFolderTransferTag(transfer->getFolderTransferTag());
    this->setAppData(transfer->getAppData());
}

MegaTransfer* MegaTransferPrivate::copy()
{
    return new MegaTransferPrivate(this);
}

void MegaTransferPrivate::setTransfer(Transfer *transfer)
{
	this->transfer = transfer;
}

Transfer* MegaTransferPrivate::getTransfer() const
{
	return transfer;
}

int MegaTransferPrivate::getTag() const
{
	return tag;
}

long long MegaTransferPrivate::getSpeed() const
{
	return speed;
}

long long MegaTransferPrivate::getDeltaSize() const
{
	return deltaSize;
}

int64_t MegaTransferPrivate::getUpdateTime() const
{
	return updateTime;
}

MegaNode *MegaTransferPrivate::getPublicNode() const
{
	return publicNode;
}

MegaNode *MegaTransferPrivate::getPublicMegaNode() const
{
    if(publicNode)
    {
        return publicNode->copy();
    }

    return NULL;
}

bool MegaTransferPrivate::isSyncTransfer() const
{
	return syncTransfer;
}

bool MegaTransferPrivate::isStreamingTransfer() const
{
    return streamingTransfer;
}

int MegaTransferPrivate::getType() const
{
	return type;
}

int64_t MegaTransferPrivate::getStartTime() const
{
	return startTime;
}

long long MegaTransferPrivate::getTransferredBytes() const
{
	return transferredBytes;
}

long long MegaTransferPrivate::getTotalBytes() const
{
	return totalBytes;
}

const char* MegaTransferPrivate::getPath() const
{
	return path;
}

const char* MegaTransferPrivate::getParentPath() const
{
	return parentPath;
}

uint64_t MegaTransferPrivate::getNodeHandle() const
{
	return nodeHandle;
}

uint64_t MegaTransferPrivate::getParentHandle() const
{
	return parentHandle;
}

long long MegaTransferPrivate::getStartPos() const
{
	return startPos;
}

long long MegaTransferPrivate::getEndPos() const
{
	return endPos;
}

int MegaTransferPrivate::getNumRetry() const
{
	return retry;
}

int MegaTransferPrivate::getMaxRetries() const
{
	return maxRetries;
}

int64_t MegaTransferPrivate::getTime() const
{
	return time;
}

const char* MegaTransferPrivate::getFileName() const
{
	return fileName;
}

char * MegaTransferPrivate::getLastBytes() const
{
    return lastBytes;
}

MegaError MegaTransferPrivate::getLastError() const
{
    return this->lastError;
}

bool MegaTransferPrivate::isFolderTransfer() const
{
    return folderTransferTag < 0;
}

int MegaTransferPrivate::getFolderTransferTag() const
{
    return this->folderTransferTag;
}

void MegaTransferPrivate::setAppData(const char *data)
{
    if (this->appData)
    {
        delete [] this->appData;
    }
    this->appData = MegaApi::strdup(data);
}

const char *MegaTransferPrivate::getAppData() const
{
    return this->appData;
}

bool MegaTransferPrivate::serialize(string *d)
{
    d->append((const char*)&type, sizeof(type));
    d->append((const char*)&nodeHandle, sizeof(nodeHandle));
    d->append((const char*)&parentHandle, sizeof(parentHandle));

    unsigned short ll;
    ll = path ? strlen(path) + 1 : 0;
    d->append((char*)&ll, sizeof(ll));
    d->append(path, ll);

    ll = parentPath ? strlen(parentPath) + 1 : 0;
    d->append((char*)&ll, sizeof(ll));
    d->append(parentPath, ll);

    ll = fileName ? strlen(fileName) + 1 : 0;
    d->append((char*)&ll, sizeof(ll));
    d->append(fileName, ll);

    d->append((const char*)&folderTransferTag, sizeof(folderTransferTag));
    d->append("\0\0\0\0\0\0", 7);

    ll = appData ? strlen(appData) + 1 : 0;
    if (ll)
    {
        char hasAppData = 1;
        d->append(&hasAppData, 1);
        d->append((char*)&ll, sizeof(ll));
        d->append(appData, ll);
    }
    else
    {
        d->append("", 1);
    }

    MegaNodePrivate *node = dynamic_cast<MegaNodePrivate *>(publicNode);
    bool isPublic = (node != NULL);
    d->append((const char*)&isPublic, sizeof(bool));
    if (isPublic)
    {
        node->serialize(d);
    }
    return true;
}

MegaTransferPrivate *MegaTransferPrivate::unserialize(string *d)
{
    const char* ptr = d->data();
    const char* end = ptr + d->size();

    if (ptr + sizeof(int) + sizeof(MegaHandle)
            + sizeof(MegaHandle) + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - data too short";
        return NULL;
    }

    int type = MemAccess::get<int>(ptr);
    ptr += sizeof(int);

    MegaTransferPrivate *transfer = new MegaTransferPrivate(type);
    transfer->nodeHandle = MemAccess::get<MegaHandle>(ptr);
    ptr += sizeof(MegaHandle);

    transfer->parentHandle = MemAccess::get<MegaHandle>(ptr);
    ptr += sizeof(MegaHandle);

    unsigned short pathlen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(unsigned short);

    if (ptr + pathlen + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - path too long";
        delete transfer;
        return NULL;
    }

    if (pathlen)
    {
        string path;
        path.assign(ptr, pathlen - 1);
        transfer->setPath(path.c_str());
    }
    ptr += pathlen;

    unsigned short parentPathLen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(unsigned short);

    if (ptr + parentPathLen + sizeof(unsigned short) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - parentpath too long";
        delete transfer;
        return NULL;
    }

    if (parentPathLen)
    {
        string path;
        path.assign(ptr, parentPathLen - 1);
        transfer->setParentPath(path.c_str());
    }
    ptr += parentPathLen;

    unsigned short fileNameLen = MemAccess::get<unsigned short>(ptr);
    ptr += sizeof(unsigned short);

    if (ptr + fileNameLen + sizeof(int) + 7 + sizeof(char) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - filename too long";
        delete transfer;
        return NULL;
    }

    if (fileNameLen)
    {
        string path;
        path.assign(ptr, fileNameLen - 1);
        transfer->setFileName(path.c_str());
    }
    ptr += fileNameLen;

    transfer->folderTransferTag = MemAccess::get<int>(ptr);
    ptr += sizeof(int);

    if (memcmp(ptr, "\0\0\0\0\0\0", 7))
    {
        LOG_err << "MegaTransfer unserialization failed - invalid version";
        delete transfer;
        return NULL;
    }
    ptr += 7;

    char hasAppData = MemAccess::get<char>(ptr);
    ptr += sizeof(char);
    if (hasAppData > 1)
    {
        LOG_err << "MegaTransfer unserialization failed - invalid app data";
        delete transfer;
        return NULL;
    }

    if (hasAppData)
    {
        if (ptr + sizeof(unsigned short) > end)
        {
            LOG_err << "MegaTransfer unserialization failed - no app data header";
            delete transfer;
            return NULL;
        }

        unsigned short appDataLen = MemAccess::get<unsigned short>(ptr);
        ptr += sizeof(unsigned short);
        if (!appDataLen || (ptr + appDataLen > end))
        {
            LOG_err << "MegaTransfer unserialization failed - invalid appData";
            delete transfer;
            return NULL;
        }

        string data;
        data.assign(ptr, appDataLen - 1);
        transfer->setAppData(data.c_str());
        ptr += appDataLen;
    }

    if (ptr + sizeof(bool) > end)
    {
        LOG_err << "MegaTransfer unserialization failed - reading public node";
        delete transfer;
        return NULL;
    }

    bool isPublic = MemAccess::get<bool>(ptr);
    ptr += sizeof(bool);

    d->erase(0, ptr - d->data());

    if (isPublic)
    {
        MegaNodePrivate *publicNode = MegaNodePrivate::unserialize(d);
        if (!publicNode)
        {
            LOG_err << "MegaTransfer unserialization failed - unable to unserialize MegaNode";
            delete transfer;
            return NULL;
        }

        transfer->setPublicNode(publicNode);
        delete publicNode;
    }

    return transfer;
}

void MegaTransferPrivate::setTag(int tag)
{
	this->tag = tag;
}

void MegaTransferPrivate::setSpeed(long long speed)
{
	this->speed = speed;
}

void MegaTransferPrivate::setDeltaSize(long long deltaSize)
{
	this->deltaSize = deltaSize;
}

void MegaTransferPrivate::setUpdateTime(int64_t updateTime)
{
	this->updateTime = updateTime;
}
void MegaTransferPrivate::setPublicNode(MegaNode *publicNode)
{
    if(this->publicNode)
    	delete this->publicNode;

    if(!publicNode)
    	this->publicNode = NULL;
    else
    	this->publicNode = publicNode->copy();
}

void MegaTransferPrivate::setSyncTransfer(bool syncTransfer)
{
    this->syncTransfer = syncTransfer;
}

void MegaTransferPrivate::setStreamingTransfer(bool streamingTransfer)
{
    this->streamingTransfer = streamingTransfer;
}

void MegaTransferPrivate::setStartTime(int64_t startTime)
{
	this->startTime = startTime;
}

void MegaTransferPrivate::setTransferredBytes(long long transferredBytes)
{
	this->transferredBytes = transferredBytes;
}

void MegaTransferPrivate::setTotalBytes(long long totalBytes)
{
	this->totalBytes = totalBytes;
}

void MegaTransferPrivate::setLastBytes(char *lastBytes)
{
    this->lastBytes = lastBytes;
}

void MegaTransferPrivate::setLastError(MegaError e)
{
    this->lastError = e;
}

void MegaTransferPrivate::setFolderTransferTag(int tag)
{
    this->folderTransferTag = tag;
}

void MegaTransferPrivate::setListener(MegaTransferListener *listener)
{
    this->listener = listener;
}

void MegaTransferPrivate::setPath(const char* path)
{
	if(this->path) delete [] this->path;
    this->path = MegaApi::strdup(path);
	if(!this->path) return;

	for(int i = strlen(path)-1; i>=0; i--)
	{
		if((path[i]=='\\') || (path[i]=='/'))
		{
			setFileName(&(path[i+1]));
            char *parentPath = MegaApi::strdup(path);
            parentPath[i+1] = '\0';
            setParentPath(parentPath);
            delete [] parentPath;
			return;
		}
	}
	setFileName(path);
}

void MegaTransferPrivate::setParentPath(const char* path)
{
	if(this->parentPath) delete [] this->parentPath;
    this->parentPath =  MegaApi::strdup(path);
}

void MegaTransferPrivate::setFileName(const char* fileName)
{
	if(this->fileName) delete [] this->fileName;
    this->fileName =  MegaApi::strdup(fileName);
}

void MegaTransferPrivate::setNodeHandle(uint64_t nodeHandle)
{
	this->nodeHandle = nodeHandle;
}

void MegaTransferPrivate::setParentHandle(uint64_t parentHandle)
{
	this->parentHandle = parentHandle;
}

void MegaTransferPrivate::setStartPos(long long startPos)
{
	this->startPos = startPos;
}

void MegaTransferPrivate::setEndPos(long long endPos)
{
	this->endPos = endPos;
}

void MegaTransferPrivate::setNumRetry(int retry)
{
	this->retry = retry;
}

void MegaTransferPrivate::setMaxRetries(int maxRetries)
{
	this->maxRetries = maxRetries;
}

void MegaTransferPrivate::setTime(int64_t time)
{
	this->time = time;
}

const char * MegaTransferPrivate::getTransferString() const
{
    switch(type)
    {
    case TYPE_UPLOAD:
        return "UPLOAD";
    case TYPE_DOWNLOAD:
        return "DOWNLOAD";
    case TYPE_LOCAL_HTTP_DOWNLOAD:
        return "LOCAL_HTTP_DOWNLOAD";
    }

    return "UNKNOWN";
}

MegaTransferListener* MegaTransferPrivate::getListener() const
{
	return listener;
}

MegaTransferPrivate::~MegaTransferPrivate()
{
	delete[] path;
	delete[] parentPath;
	delete [] fileName;
    delete publicNode;
}

const char * MegaTransferPrivate::toString() const
{
	return getTransferString();
}

const char * MegaTransferPrivate::__str__() const
{
	return getTransferString();
}

const char *MegaTransferPrivate::__toString() const
{
	return getTransferString();
}

MegaContactRequestPrivate::MegaContactRequestPrivate(PendingContactRequest *request)
{
    handle = request->id;
    sourceEmail = request->originatoremail.size() ? MegaApi::strdup(request->originatoremail.c_str()) : NULL;
    sourceMessage = request->msg.size() ? MegaApi::strdup(request->msg.c_str()) : NULL;
    targetEmail = request->targetemail.size() ? MegaApi::strdup(request->targetemail.c_str()) : NULL;
    creationTime = request->ts;
    modificationTime = request->uts;

    if(request->changed.accepted)
    {
        status = MegaContactRequest::STATUS_ACCEPTED;
    }
    else if(request->changed.deleted)
    {
        status = MegaContactRequest::STATUS_DELETED;
    }
    else if(request->changed.denied)
    {
        status = MegaContactRequest::STATUS_DENIED;
    }
    else if(request->changed.ignored)
    {
        status = MegaContactRequest::STATUS_IGNORED;
    }
    else if(request->changed.reminded)
    {
        status = MegaContactRequest::STATUS_REMINDED;
    }
    else
    {
        status = MegaContactRequest::STATUS_UNRESOLVED;
    }

    outgoing = request->isoutgoing;
}

MegaContactRequestPrivate::MegaContactRequestPrivate(const MegaContactRequest *request)
{
    handle = request->getHandle();
    sourceEmail = MegaApi::strdup(request->getSourceEmail());
    sourceMessage = MegaApi::strdup(request->getSourceMessage());
    targetEmail = MegaApi::strdup(request->getTargetEmail());
    creationTime = request->getCreationTime();
    modificationTime = request->getModificationTime();
    status = request->getStatus();
    outgoing = request->isOutgoing();
}

MegaContactRequestPrivate::~MegaContactRequestPrivate()
{
    delete [] sourceEmail;
    delete [] sourceMessage;
    delete [] targetEmail;
}

MegaContactRequest *MegaContactRequestPrivate::fromContactRequest(PendingContactRequest *request)
{
    return new MegaContactRequestPrivate(request);
}

MegaContactRequest *MegaContactRequestPrivate::copy() const
{
    return new MegaContactRequestPrivate(this);
}

MegaHandle MegaContactRequestPrivate::getHandle() const
{
    return handle;
}

char *MegaContactRequestPrivate::getSourceEmail() const
{
    return sourceEmail;
}

char *MegaContactRequestPrivate::getSourceMessage() const
{
    return sourceMessage;
}

char *MegaContactRequestPrivate::getTargetEmail() const
{
    return targetEmail;
}

int64_t MegaContactRequestPrivate::getCreationTime() const
{
    return creationTime;
}

int64_t MegaContactRequestPrivate::getModificationTime() const
{
    return modificationTime;
}

int MegaContactRequestPrivate::getStatus() const
{
    return status;
}

bool MegaContactRequestPrivate::isOutgoing() const
{
    return outgoing;
}


MegaAccountDetails *MegaAccountDetailsPrivate::fromAccountDetails(AccountDetails *details)
{
    return new MegaAccountDetailsPrivate(details);
}

MegaAccountDetailsPrivate::MegaAccountDetailsPrivate(AccountDetails *details)
{
    this->details = (*details);
}

MegaAccountDetailsPrivate::~MegaAccountDetailsPrivate()
{ }

MegaRequest *MegaRequestPrivate::copy()
{
    return new MegaRequestPrivate(this);
}

MegaRequestPrivate::MegaRequestPrivate(int type, MegaRequestListener *listener)
{
	this->type = type;
    this->tag = 0;
	this->transfer = 0;
	this->listener = listener;
#ifdef ENABLE_SYNC
    this->syncListener = NULL;
#endif
	this->nodeHandle = UNDEF;
	this->link = NULL;
	this->parentHandle = UNDEF;
    this->sessionKey = NULL;
	this->name = NULL;
	this->email = NULL;
    this->text = NULL;
	this->password = NULL;
	this->newPassword = NULL;
	this->privateKey = NULL;
	this->access = MegaShare::ACCESS_UNKNOWN;
	this->numRetry = 0;
	this->publicNode = NULL;
	this->numDetails = 0;
	this->file = NULL;
	this->attrType = 0;
    this->flag = false;
    this->totalBytes = -1;
    this->transferredBytes = 0;
    this->number = 0;

    if(type == MegaRequest::TYPE_ACCOUNT_DETAILS)
    {
        this->accountDetails = new AccountDetails();
    }
    else
    {
        this->accountDetails = NULL;
    }

    if((type == MegaRequest::TYPE_GET_PRICING) || (type == MegaRequest::TYPE_GET_PAYMENT_ID) || type == MegaRequest::TYPE_UPGRADE_ACCOUNT)
    {
        this->megaPricing = new MegaPricingPrivate();
    }
    else
    {
        megaPricing = NULL;
    }

#ifdef ENABLE_CHAT
    if(type == MegaRequest::TYPE_CHAT_CREATE)
    {
        this->chatPeerList = new MegaTextChatPeerListPrivate();
    }
    else
    {
        this->chatPeerList = NULL;
    }

    if(type == MegaRequest::TYPE_CHAT_FETCH)
    {
        this->chatList = new MegaTextChatListPrivate();
    }
    else
    {
        this->chatList = NULL;
    }
#endif
}

MegaRequestPrivate::MegaRequestPrivate(MegaRequestPrivate *request)
{
    this->link = NULL;
    this->sessionKey = NULL;
    this->name = NULL;
    this->email = NULL;
    this->text = NULL;
    this->password = NULL;
    this->newPassword = NULL;
    this->privateKey = NULL;
    this->access = MegaShare::ACCESS_UNKNOWN;
    this->publicNode = NULL;
    this->file = NULL;
    this->publicNode = NULL;

    this->type = request->getType();
    this->setTag(request->getTag());
    this->setNodeHandle(request->getNodeHandle());
    this->setLink(request->getLink());
    this->setParentHandle(request->getParentHandle());
    this->setSessionKey(request->getSessionKey());
    this->setName(request->getName());
    this->setEmail(request->getEmail());
    this->setPassword(request->getPassword());
    this->setNewPassword(request->getNewPassword());
    this->setPrivateKey(request->getPrivateKey());
    this->setAccess(request->getAccess());
    this->setNumRetry(request->getNumRetry());
	this->numDetails = 0;
    this->setFile(request->getFile());
    this->setParamType(request->getParamType());
    this->setText(request->getText());
    this->setNumber(request->getNumber());
    this->setPublicNode(request->getPublicNode());
    this->setFlag(request->getFlag());
    this->setTransferTag(request->getTransferTag());
    this->setTotalBytes(request->getTotalBytes());
    this->setTransferredBytes(request->getTransferredBytes());
    this->listener = request->getListener();
#ifdef ENABLE_SYNC
    this->syncListener = request->getSyncListener();
#endif
    this->megaPricing = (MegaPricingPrivate *)request->getPricing();

    this->accountDetails = NULL;
    if(request->getAccountDetails())
    {
		this->accountDetails = new AccountDetails();
        *(this->accountDetails) = *(request->getAccountDetails());
	}

#ifdef ENABLE_CHAT   
    this->chatPeerList = request->getMegaTextChatPeerList() ? request->chatPeerList->copy() : NULL;
    this->chatList = request->getMegaTextChatList() ? request->chatList->copy() : NULL;
#endif

}

AccountDetails *MegaRequestPrivate::getAccountDetails() const
{
    return accountDetails;
}

#ifdef ENABLE_CHAT
MegaTextChatPeerList *MegaRequestPrivate::getMegaTextChatPeerList() const
{
    return chatPeerList;
}

void MegaRequestPrivate::setMegaTextChatPeerList(MegaTextChatPeerList *chatPeers)
{
    if (this->chatPeerList)
        delete this->chatPeerList;

    this->chatPeerList = chatPeers->copy();
}

MegaTextChatList *MegaRequestPrivate::getMegaTextChatList() const
{
    return chatList;
}

void MegaRequestPrivate::setMegaTextChatList(MegaTextChatList *chatList)
{
    if (this->chatList)
        delete this->chatList;

    this->chatList = chatList->copy();
}
#endif

#ifdef ENABLE_SYNC
void MegaRequestPrivate::setSyncListener(MegaSyncListener *syncListener)
{
    this->syncListener = syncListener;
}

MegaSyncListener *MegaRequestPrivate::getSyncListener() const
{
    return syncListener;
}
#endif

MegaAccountDetails *MegaRequestPrivate::getMegaAccountDetails() const
{
    if(accountDetails)
    {
        return MegaAccountDetailsPrivate::fromAccountDetails(accountDetails);
    }
    return NULL;
}

MegaRequestPrivate::~MegaRequestPrivate()
{
	delete [] link;
	delete [] name;
	delete [] email;
	delete [] password;
	delete [] newPassword;
	delete [] privateKey;
    delete [] sessionKey;
	delete publicNode;
	delete [] file;
	delete accountDetails;
    delete megaPricing;
    delete [] text;

#ifdef ENABLE_CHAT
    delete chatPeerList;
    delete chatList;
#endif
}

int MegaRequestPrivate::getType() const
{
	return type;
}

uint64_t MegaRequestPrivate::getNodeHandle() const
{
	return nodeHandle;
}

const char* MegaRequestPrivate::getLink() const
{
	return link;
}

uint64_t MegaRequestPrivate::getParentHandle() const
{
	return parentHandle;
}

const char* MegaRequestPrivate::getSessionKey() const
{
	return sessionKey;
}

const char* MegaRequestPrivate::getName() const
{
	return name;
}

const char* MegaRequestPrivate::getEmail() const
{
	return email;
}

const char* MegaRequestPrivate::getPassword() const
{
	return password;
}

const char* MegaRequestPrivate::getNewPassword() const
{
	return newPassword;
}

const char* MegaRequestPrivate::getPrivateKey() const
{
	return privateKey;
}

int MegaRequestPrivate::getAccess() const
{
	return access;
}

const char* MegaRequestPrivate::getFile() const
{
	return file;
}

int MegaRequestPrivate::getParamType() const
{
	return attrType;
}

const char *MegaRequestPrivate::getText() const
{
    return text;
}

long long MegaRequestPrivate::getNumber() const
{
    return number;
}

bool MegaRequestPrivate::getFlag() const
{
	return flag;
}

long long MegaRequestPrivate::getTransferredBytes() const
{
	return transferredBytes;
}

long long MegaRequestPrivate::getTotalBytes() const
{
	return totalBytes;
}

int MegaRequestPrivate::getNumRetry() const
{
	return numRetry;
}

int MegaRequestPrivate::getNumDetails() const
{
    return numDetails;
}

int MegaRequestPrivate::getTag() const
{
    return tag;
}

MegaPricing *MegaRequestPrivate::getPricing() const
{
    return megaPricing ? megaPricing->copy() : NULL;
}

void MegaRequestPrivate::setNumDetails(int numDetails)
{
	this->numDetails = numDetails;
}

MegaNode *MegaRequestPrivate::getPublicNode() const
{
	return publicNode;
}

MegaNode *MegaRequestPrivate::getPublicMegaNode() const
{
    if(publicNode)
    {
        return publicNode->copy();
    }

    return NULL;
}

void MegaRequestPrivate::setNodeHandle(uint64_t nodeHandle)
{
	this->nodeHandle = nodeHandle;
}

void MegaRequestPrivate::setParentHandle(uint64_t parentHandle)
{
	this->parentHandle = parentHandle;
}

void MegaRequestPrivate::setSessionKey(const char* sessionKey)
{
    if(this->sessionKey) delete [] this->sessionKey;
    this->sessionKey = MegaApi::strdup(sessionKey);
}

void MegaRequestPrivate::setNumRetry(int numRetry)
{
	this->numRetry = numRetry;
}

void MegaRequestPrivate::setLink(const char* link)
{
	if(this->link)
		delete [] this->link;

    this->link = MegaApi::strdup(link);
}
void MegaRequestPrivate::setName(const char* name)
{
	if(this->name)
		delete [] this->name;

    this->name = MegaApi::strdup(name);
}
void MegaRequestPrivate::setEmail(const char* email)
{
	if(this->email)
		delete [] this->email;

    this->email = MegaApi::strdup(email);
}
void MegaRequestPrivate::setPassword(const char* password)
{
	if(this->password)
		delete [] this->password;

    this->password = MegaApi::strdup(password);
}
void MegaRequestPrivate::setNewPassword(const char* newPassword)
{
	if(this->newPassword)
		delete [] this->newPassword;

    this->newPassword = MegaApi::strdup(newPassword);
}
void MegaRequestPrivate::setPrivateKey(const char* privateKey)
{
	if(this->privateKey)
		delete [] this->privateKey;

    this->privateKey = MegaApi::strdup(privateKey);
}
void MegaRequestPrivate::setAccess(int access)
{
	this->access = access;
}

void MegaRequestPrivate::setFile(const char* file)
{
    if(this->file)
        delete [] this->file;

    this->file = MegaApi::strdup(file);
}

void MegaRequestPrivate::setParamType(int type)
{
    this->attrType = type;
}

void MegaRequestPrivate::setText(const char *text)
{
    if(this->text) delete [] this->text;
    this->text = MegaApi::strdup(text);
}

void MegaRequestPrivate::setNumber(long long number)
{
    this->number = number;
}

void MegaRequestPrivate::setFlag(bool flag)
{
    this->flag = flag;
}

void MegaRequestPrivate::setTransferTag(int transfer)
{
    this->transfer = transfer;
}

void MegaRequestPrivate::setListener(MegaRequestListener *listener)
{
    this->listener = listener;
}

void MegaRequestPrivate::setTotalBytes(long long totalBytes)
{
    this->totalBytes = totalBytes;
}

void MegaRequestPrivate::setTransferredBytes(long long transferredBytes)
{
    this->transferredBytes = transferredBytes;
}

void MegaRequestPrivate::setTag(int tag)
{
    this->tag = tag;
}

void MegaRequestPrivate::addProduct(handle product, int proLevel, int gbStorage, int gbTransfer, int months, int amount, const char *currency, const char* description, const char* iosid, const char* androidid)
{
    if(megaPricing)
    {
        megaPricing->addProduct(product, proLevel, gbStorage, gbTransfer, months, amount, currency, description, iosid, androidid);
    }
}

void MegaRequestPrivate::setProxy(Proxy *proxy)
{
    this->proxy = proxy;
}

Proxy *MegaRequestPrivate::getProxy()
{
    return proxy;
}

void MegaRequestPrivate::setPublicNode(MegaNode *publicNode)
{
    if(this->publicNode)
		delete this->publicNode;

    if(!publicNode)
		this->publicNode = NULL;
    else
		this->publicNode = publicNode->copy();
}

const char *MegaRequestPrivate::getRequestString() const
{
	switch(type)
	{
        case TYPE_LOGIN: return "LOGIN";
        case TYPE_CREATE_FOLDER: return "CREATE_FOLDER";
        case TYPE_MOVE: return "MOVE";
        case TYPE_COPY: return "COPY";
        case TYPE_RENAME: return "RENAME";
        case TYPE_REMOVE: return "REMOVE";
        case TYPE_SHARE: return "SHARE";
        case TYPE_IMPORT_LINK: return "IMPORT_LINK";
        case TYPE_EXPORT: return "EXPORT";
        case TYPE_FETCH_NODES: return "FETCH_NODES";
        case TYPE_ACCOUNT_DETAILS: return "ACCOUNT_DETAILS";
        case TYPE_CHANGE_PW: return "CHANGE_PW";
        case TYPE_UPLOAD: return "UPLOAD";
        case TYPE_LOGOUT: return "LOGOUT";
        case TYPE_GET_PUBLIC_NODE: return "GET_PUBLIC_NODE";
        case TYPE_GET_ATTR_FILE: return "GET_ATTR_FILE";
        case TYPE_SET_ATTR_FILE: return "SET_ATTR_FILE";
        case TYPE_GET_ATTR_USER: return "GET_ATTR_USER";
        case TYPE_SET_ATTR_USER: return "SET_ATTR_USER";
        case TYPE_RETRY_PENDING_CONNECTIONS: return "RETRY_PENDING_CONNECTIONS";
        case TYPE_REMOVE_CONTACT: return "REMOVE_CONTACT";
        case TYPE_CREATE_ACCOUNT: return "CREATE_ACCOUNT";
        case TYPE_CONFIRM_ACCOUNT: return "CONFIRM_ACCOUNT";
        case TYPE_QUERY_SIGNUP_LINK: return "QUERY_SIGNUP_LINK";
        case TYPE_ADD_SYNC: return "ADD_SYNC";
        case TYPE_REMOVE_SYNC: return "REMOVE_SYNC";
        case TYPE_REMOVE_SYNCS: return "REMOVE_SYNCS";
        case TYPE_PAUSE_TRANSFERS: return "PAUSE_TRANSFERS";
        case TYPE_CANCEL_TRANSFER: return "CANCEL_TRANSFER";
        case TYPE_CANCEL_TRANSFERS: return "CANCEL_TRANSFERS";
        case TYPE_DELETE: return "DELETE";
        case TYPE_REPORT_EVENT: return "REPORT_EVENT";
        case TYPE_CANCEL_ATTR_FILE: return "CANCEL_ATTR_FILE";
        case TYPE_GET_PRICING: return "GET_PRICING";
        case TYPE_GET_PAYMENT_ID: return "GET_PAYMENT_ID";
        case TYPE_UPGRADE_ACCOUNT: return "UPGRADE_ACCOUNT";
        case TYPE_GET_USER_DATA: return "GET_USER_DATA";
        case TYPE_LOAD_BALANCING: return "LOAD_BALANCING";
        case TYPE_KILL_SESSION: return "KILL_SESSION";
        case TYPE_SUBMIT_PURCHASE_RECEIPT: return "SUBMIT_PURCHASE_RECEIPT";
        case TYPE_CREDIT_CARD_STORE: return "CREDIT_CARD_STORE";
        case TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS: return "CREDIT_CARD_QUERY_SUBSCRIPTIONS";
        case TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS: return "CREDIT_CARD_CANCEL_SUBSCRIPTIONS";
        case TYPE_GET_SESSION_TRANSFER_URL: return "GET_SESSION_TRANSFER_URL";
        case TYPE_GET_PAYMENT_METHODS: return "GET_PAYMENT_METHODS";
        case TYPE_INVITE_CONTACT: return "INVITE_CONTACT";
        case TYPE_REPLY_CONTACT_REQUEST: return "REPLY_CONTACT_REQUEST";
        case TYPE_SUBMIT_FEEDBACK: return "SUBMIT_FEEDBACK";
        case TYPE_SEND_EVENT: return "SEND_EVENT";
        case TYPE_CLEAN_RUBBISH_BIN: return "CLEAN_RUBBISH_BIN";
        case TYPE_SET_ATTR_NODE: return "SET_ATTR_NODE";
        case TYPE_CHAT_CREATE: return "CHAT_CREATE";
        case TYPE_CHAT_FETCH: return "CHAT_FETCH";
        case TYPE_CHAT_INVITE: return "CHAT_INVITE";
        case TYPE_CHAT_REMOVE: return "CHAT_REMOVE";
        case TYPE_CHAT_URL: return "CHAT_URL";
        case TYPE_CHAT_GRANT_ACCESS: return "CHAT_GRANT_ACCESS";
        case TYPE_CHAT_REMOVE_ACCESS: return "CHAT_REMOVE_ACCESS";
        case TYPE_USE_HTTPS_ONLY: return "USE_HTTPS_ONLY";
        case TYPE_SET_PROXY: return "SET_PROXY";
        case TYPE_GET_RECOVERY_LINK: return "TYPE_GET_RECOVERY_LINK";
        case TYPE_QUERY_RECOVERY_LINK: return "TYPE_QUERY_RECOVERY_LINK";
        case TYPE_CONFIRM_RECOVERY_LINK: return "TYPE_CONFIRM_RECOVERY_LINK";
        case TYPE_GET_CANCEL_LINK: return "TYPE_GET_CANCEL_LINK";
        case TYPE_CONFIRM_CANCEL_LINK: return "TYPE_CONFIRM_CANCEL_LINK";
        case TYPE_GET_CHANGE_EMAIL_LINK: return "TYPE_GET_CHANGE_EMAIL_LINK";
        case TYPE_CONFIRM_CHANGE_EMAIL_LINK: return "TYPE_CONFIRM_CHANGE_EMAIL_LINK";
    }
    return "UNKNOWN";
}

MegaRequestListener *MegaRequestPrivate::getListener() const
{
	return listener;
}

int MegaRequestPrivate::getTransferTag() const
{
	return transfer;
}

const char *MegaRequestPrivate::toString() const
{
	return getRequestString();
}

const char *MegaRequestPrivate::__str__() const
{
	return getRequestString();
}

const char *MegaRequestPrivate::__toString() const
{
	return getRequestString();
}

MegaStringListPrivate::MegaStringListPrivate()
{
    list = NULL;
    s = 0;
}

MegaStringListPrivate::MegaStringListPrivate(MegaStringListPrivate *stringList)
{
    s = stringList->size();
    if (!s)
    {
        list = NULL;
        return;
    }

    list = new const char*[s];
    for (int i = 0; i < s; i++)
        list[i] = MegaApi::strdup(stringList->get(i));
}

MegaStringListPrivate::MegaStringListPrivate(char **newlist, int size)
{
    list = NULL;
    s = size;
    if (!size)
    {
        return;
    }

    list = new const char*[size];
    for (int i = 0; i < size; i++)
        list[i] = newlist[i];
}

MegaStringListPrivate::~MegaStringListPrivate()
{
    if(!list)
        return;

    for(int i=0; i<s; i++)
        delete [] list[i];
    delete [] list;
}

MegaStringList *MegaStringListPrivate::copy()
{
    return new MegaStringListPrivate(this);
}

const char *MegaStringListPrivate::get(int i)
{
    if(!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaStringListPrivate::size()
{
    return s;
}

MegaNodeListPrivate::MegaNodeListPrivate()
{
	list = NULL;
	s = 0;
}

MegaNodeListPrivate::MegaNodeListPrivate(Node** newlist, int size)
{
	list = NULL; s = size;
	if(!size) return;

	list = new MegaNode*[size];
	for(int i=0; i<size; i++)
		list[i] = MegaNodePrivate::fromNode(newlist[i]);
}

MegaNodeListPrivate::MegaNodeListPrivate(MegaNodeListPrivate *nodeList)
{
    s = nodeList->size();
	if (!s)
	{
		list = NULL;
		return;
	}

	list = new MegaNode*[s];
	for (int i = 0; i<s; i++)
        list[i] = new MegaNodePrivate(nodeList->get(i));
}

MegaNodeListPrivate::~MegaNodeListPrivate()
{
	if(!list)
		return;

	for(int i=0; i<s; i++)
		delete list[i];
	delete [] list;
}

MegaNodeList *MegaNodeListPrivate::copy()
{
    return new MegaNodeListPrivate(this);
}

MegaNode *MegaNodeListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int MegaNodeListPrivate::size()
{
	return s;
}

MegaUserListPrivate::MegaUserListPrivate()
{
	list = NULL;
	s = 0;
}

MegaUserListPrivate::MegaUserListPrivate(User** newlist, int size)
{
	list = NULL;
	s = size;

	if(!size)
		return;

	list = new MegaUser*[size];
	for(int i=0; i<size; i++)
		list[i] = MegaUserPrivate::fromUser(newlist[i]);
}

MegaUserListPrivate::MegaUserListPrivate(MegaUserListPrivate *userList)
{
    s = userList->size();
	if (!s)
	{
		list = NULL;
		return;
	}
	list = new MegaUser*[s];
	for (int i = 0; i<s; i++)
        list[i] = new MegaUserPrivate(userList->get(i));
}

MegaUserListPrivate::~MegaUserListPrivate()
{
	if(!list)
		return;

	for(int i=0; i<s; i++)
		delete list[i];

	delete [] list;
}

MegaUserList *MegaUserListPrivate::copy()
{
    return new MegaUserListPrivate(this);
}

MegaUser *MegaUserListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int MegaUserListPrivate::size()
{
	return s;
}


MegaShareListPrivate::MegaShareListPrivate()
{
	list = NULL;
	s = 0;
}

MegaShareListPrivate::MegaShareListPrivate(Share** newlist, uint64_t *uint64_tlist, int size)
{
	list = NULL; s = size;
	if(!size) return;

	list = new MegaShare*[size];
	for(int i=0; i<size; i++)
        list[i] = MegaSharePrivate::fromShare(uint64_tlist[i], newlist[i]);
}

MegaShareListPrivate::~MegaShareListPrivate()
{
	if(!list)
		return;

	for(int i=0; i<s; i++)
		delete list[i];

	delete [] list;
}

MegaShare *MegaShareListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int MegaShareListPrivate::size()
{
	return s;
}

MegaTransferListPrivate::MegaTransferListPrivate()
{
	list = NULL;
	s = 0;
}

MegaTransferListPrivate::MegaTransferListPrivate(MegaTransfer** newlist, int size)
{
    list = NULL;
    s = size;

    if(!size)
        return;

    list = new MegaTransfer*[size];
    for(int i=0; i<size; i++)
        list[i] = newlist[i]->copy();
}

MegaTransferListPrivate::~MegaTransferListPrivate()
{
	if(!list)
		return;

    for(int i=0; i < s; i++)
		delete list[i];

	delete [] list;
}

MegaTransfer *MegaTransferListPrivate::get(int i)
{
	if(!list || (i < 0) || (i >= s))
		return NULL;

	return list[i];
}

int MegaTransferListPrivate::size()
{
	return s;
}

MegaContactRequestListPrivate::MegaContactRequestListPrivate()
{
    list = NULL;
    s = 0;
}

MegaContactRequestListPrivate::MegaContactRequestListPrivate(PendingContactRequest **newlist, int size)
{
    list = NULL;
    s = size;

    if(!size)
        return;

    list = new MegaContactRequest*[size];
    for(int i=0; i<size; i++)
        list[i] = new MegaContactRequestPrivate(newlist[i]);
}

MegaContactRequestListPrivate::~MegaContactRequestListPrivate()
{
    if(!list)
        return;

    for(int i=0; i < s; i++)
        delete list[i];

    delete [] list;
}

MegaContactRequestList *MegaContactRequestListPrivate::copy()
{
    return new MegaContactRequestListPrivate(this);
}

MegaContactRequest *MegaContactRequestListPrivate::get(int i)
{
    if(!list || (i < 0) || (i >= s))
        return NULL;

    return list[i];
}

int MegaContactRequestListPrivate::size()
{
    return s;
}

MegaContactRequestListPrivate::MegaContactRequestListPrivate(MegaContactRequestListPrivate *requestList)
{
    s = requestList->size();
    if (!s)
    {
        list = NULL;
        return;
    }
    list = new MegaContactRequest*[s];
    for (int i = 0; i < s; i++)
        list[i] = new MegaContactRequestPrivate(requestList->get(i));
}

int MegaFile::nextseqno = 0;

MegaFile::MegaFile() : File()
{
    seqno = ++nextseqno;
    megaTransfer = NULL;
}

void MegaFile::setTransfer(MegaTransferPrivate *transfer)
{
    this->megaTransfer = transfer;
}

MegaTransferPrivate *MegaFile::getTransfer()
{
    return megaTransfer;
}

bool MegaFile::serialize(string *d)
{
    if (!megaTransfer)
    {
        return false;
    }

    if (!File::serialize(d))
    {
        return false;
    }

    if (!megaTransfer->serialize(d))
    {
        return false;
    }

    d->append("\0\0\0\0\0\0\0", 8);

    return true;
}

MegaFile *MegaFile::unserialize(string *d)
{
    File *file = File::unserialize(d);
    if (!file)
    {
        LOG_err << "Error unserializing MegaFile: Unable to unserialize File";
        return NULL;
    }

    MegaFile *megaFile = new MegaFile();
    *(File *)megaFile = *(File *)file;
    delete file;

    MegaTransferPrivate *transfer = MegaTransferPrivate::unserialize(d);
    if (!transfer)
    {
        delete megaFile;
        return NULL;
    }

    const char* ptr = d->data();
    const char* end = ptr + d->size();
    if (ptr + 8 > end)
    {
        LOG_err << "MegaFile unserialization failed - data too short";
        delete megaFile;
        delete transfer;
        return NULL;
    }

    if (memcmp(ptr, "\0\0\0\0\0\0\0", 8))
    {
        LOG_err << "MegaFile unserialization failed - invalid version";
        delete megaFile;
        delete transfer;
        return NULL;
    }
    ptr += 8;

    d->erase(0, ptr - d->data());

    megaFile->setTransfer(transfer);
    return megaFile;
}

MegaFileGet::MegaFileGet(MegaClient *client, Node *n, string dstPath) : MegaFile()
{
    h = n->nodehandle;
    *(FileFingerprint*)this = *n;

    string securename = n->displayname();
    client->fsaccess->name2local(&securename);
    client->fsaccess->local2path(&securename, &name);

    string finalPath;
    if(dstPath.size())
    {
        char c = dstPath[dstPath.size()-1];
        if((c == '\\') || (c == '/')) finalPath = dstPath+name;
        else finalPath = dstPath;
    }
    else finalPath = name;

    size = n->size;
    mtime = n->mtime;

    if(n->nodekey.size()>=sizeof(filekey))
        memcpy(filekey,n->nodekey.data(),sizeof filekey);

    client->fsaccess->path2local(&finalPath, &localname);
    hprivate = true;
    hforeign = false;
}

MegaFileGet::MegaFileGet(MegaClient *client, MegaNode *n, string dstPath) : MegaFile()
{
    h = n->getHandle();
    name = n->getName();
	string finalPath;
	if(dstPath.size())
	{
		char c = dstPath[dstPath.size()-1];
		if((c == '\\') || (c == '/')) finalPath = dstPath+name;
		else finalPath = dstPath;
	}
	else finalPath = name;

    const char *fingerprint = n->getFingerprint();
    if (fingerprint)
    {
        FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
        if (fp)
        {
            *(FileFingerprint *)this = *(FileFingerprint *)fp;
            delete fp;
        }
    }

    size = n->getSize();
    mtime = n->getModificationTime();

    if(n->getNodeKey()->size()>=sizeof(filekey))
        memcpy(filekey,n->getNodeKey()->data(),sizeof filekey);

    client->fsaccess->path2local(&finalPath, &localname);
    hprivate = !n->isPublic();
    hforeign = n->isForeign();

    if(n->getPrivateAuth()->size())
    {
        privauth = *n->getPrivateAuth();
    }

    if(n->getPublicAuth()->size())
    {
        pubauth = *n->getPublicAuth();
    }
}

bool MegaFileGet::serialize(string *d)
{
    if (!MegaFile::serialize(d))
    {
        return false;
    }

    d->append("\0\0\0\0\0\0\0", 8);

    return true;
}

MegaFileGet *MegaFileGet::unserialize(string *d)
{
    MegaFile *file = MegaFile::unserialize(d);
    if (!file)
    {
        LOG_err << "Error unserializing MegaFileGet: Unable to unserialize MegaFile";
        return NULL;
    }

    const char* ptr = d->data();
    const char* end = ptr + d->size();
    if (ptr + 8 > end)
    {
        LOG_err << "MegaFileGet unserialization failed - data too short";
        delete file;
        return NULL;
    }

    if (memcmp(ptr, "\0\0\0\0\0\0\0", 8))
    {
        LOG_err << "MegaFileGet unserialization failed - invalid version";
        delete file;
        return NULL;
    }

    ptr += 8;
    if (ptr != end)
    {
        LOG_err << "MegaFileGet unserialization failed - wrong size";
        delete file;
        return NULL;
    }

    MegaFileGet *megaFile = new MegaFileGet();
    *(MegaFile *)megaFile = *(MegaFile *)file;
    delete file;

    return megaFile;
}

void MegaFileGet::prepare()
{
    if (!transfer->localfilename.size())
    {
        transfer->localfilename = localname;

        size_t index =  string::npos;
        while ((index = transfer->localfilename.rfind(transfer->client->fsaccess->localseparator, index)) != string::npos)
        {
            if(!(index % transfer->client->fsaccess->localseparator.size()))
            {
                break;
            }

            index--;
        }

        if(index != string::npos)
        {
            transfer->localfilename.resize(index + transfer->client->fsaccess->localseparator.size());
        }

        string suffix;
        transfer->client->fsaccess->tmpnamelocal(&suffix);
        transfer->localfilename.append(suffix);
    }
}

void MegaFileGet::updatelocalname()
{
#ifdef _WIN32
    transfer->localfilename.append("", 1);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.data(), GetFileExInfoStandard, &fad))
        SetFileAttributesW((LPCWSTR)transfer->localfilename.data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
    transfer->localfilename.resize(transfer->localfilename.size()-1);
#endif
}

void MegaFileGet::progress()
{
#ifdef _WIN32
    if(transfer->slot && !transfer->slot->progressreported)
    {
        transfer->localfilename.append("", 1);
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.data(), GetFileExInfoStandard, &fad))
            SetFileAttributesW((LPCWSTR)transfer->localfilename.data(), fad.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
        transfer->localfilename.resize(transfer->localfilename.size()-1);
    }
#endif
}

void MegaFileGet::completed(Transfer*, LocalNode*)
{
    delete this;
}

void MegaFileGet::terminated()
{
    delete this;
}

MegaFilePut::MegaFilePut(MegaClient *, string* clocalname, string *filename, handle ch, const char* ctargetuser, int64_t mtime) : MegaFile()
{
    // full local path
    localname = *clocalname;

    // target parent node
    h = ch;

    // target user
    targetuser = ctargetuser;

    // new node name
    name = *filename;

    customMtime = mtime;
}

bool MegaFilePut::serialize(string *d)
{
    if (!MegaFile::serialize(d))
    {
        return false;
    }

    d->append((char*)&customMtime, sizeof(customMtime));
    d->append("\0\0\0\0\0\0\0", 8);

    return true;
}

MegaFilePut *MegaFilePut::unserialize(string *d)
{
    MegaFile *file = MegaFile::unserialize(d);
    if (!file)
    {
        LOG_err << "Error unserializing MegaFilePut: Unable to unserialize MegaFile";
        return NULL;
    }

    const char* ptr = d->data();
    const char* end = ptr + d->size();
    if (ptr + sizeof(int64_t) + 8 > end)
    {
        LOG_err << "MegaFilePut unserialization failed - data too short";
        delete file;
        return NULL;
    }

    int64_t customMtime = MemAccess::get<int64_t>(ptr);
    ptr += sizeof(customMtime);

    if (memcmp(ptr, "\0\0\0\0\0\0\0", 8))
    {
        LOG_err << "MegaFilePut unserialization failed - invalid version";
        delete file;
        return NULL;
    }

    ptr += 8;
    if (ptr != end)
    {
        LOG_err << "MegaFilePut unserialization failed - wrong size";
        delete file;
        return NULL;
    }

    MegaFilePut *megaFile = new MegaFilePut();
    *(MegaFile *)megaFile = *(MegaFile *)file;
    delete file;

    megaFile->customMtime = customMtime;
    return megaFile;
}

void MegaFilePut::completed(Transfer* t, LocalNode*)
{
    if(customMtime >= 0)
        t->mtime = customMtime;

    File::completed(t,NULL);
    delete this;
}

void MegaFilePut::terminated()
{
    delete this;
}

bool TreeProcessor::processNode(Node*)
{
	return false; /* Stops the processing */
}

TreeProcessor::~TreeProcessor()
{ }


//Entry point for the blocking thread
void *MegaApiImpl::threadEntryPoint(void *param)
{
#ifndef _WIN32
    struct sigaction noaction;
    memset(&noaction, 0, sizeof(noaction));
    noaction.sa_handler = SIG_IGN;
    ::sigaction(SIGPIPE, &noaction, 0);
#endif

    MegaApiImpl *megaApiImpl = (MegaApiImpl *)param;
    megaApiImpl->loop();
	return 0;
}

ExternalLogger *MegaApiImpl::externalLogger = NULL;

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath, const char *userAgent)
{
	init(api, appKey, processor, basePath, userAgent);
}

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, const char *basePath, const char *userAgent)
{
	init(api, appKey, NULL, basePath, userAgent);
}

MegaApiImpl::MegaApiImpl(MegaApi *api, const char *appKey, const char *basePath, const char *userAgent, int fseventsfd)
{
	init(api, appKey, NULL, basePath, userAgent, fseventsfd);
}

void MegaApiImpl::init(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath, const char *userAgent, int fseventsfd)
{
    this->api = api;

    sdkMutex.init(true);
    maxRetries = 7;
	currentTransfer = NULL;
    pendingUploads = 0;
    pendingDownloads = 0;
    totalUploads = 0;
    totalDownloads = 0;
    client = NULL;
    waiting = false;
    waitingRequest = false;
    totalDownloadedBytes = 0;
    totalUploadedBytes = 0;
    activeRequest = NULL;
    activeTransfer = NULL;
    activeError = NULL;
    activeNodes = NULL;
    activeUsers = NULL;
    syncLowerSizeLimit = 0;
    syncUpperSizeLimit = 0;
    downloadSpeed = 0;
    uploadSpeed = 0;
    uploadPartialBytes = 0;
    downloadPartialBytes = 0;

#ifdef HAVE_LIBUV
    httpServer = NULL;
    httpServerMaxBufferSize = 0;
    httpServerMaxOutputSize = 0;
    httpServerEnableFiles = true;
    httpServerEnableFolders = false;
    httpServerRestrictedMode = MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
    httpServerSubtitlesSupportEnabled = false;
#endif

    httpio = new MegaHttpIO();
    waiter = new MegaWaiter();

#ifndef __APPLE__
    (void)fseventsfd;
    fsAccess = new MegaFileSystemAccess();
#else
    fsAccess = new MegaFileSystemAccess(fseventsfd);
#endif

	if (basePath)
	{
		string sBasePath = basePath;
		int lastIndex = sBasePath.size() - 1;
		if (sBasePath[lastIndex] != '/' && sBasePath[lastIndex] != '\\')
		{
			string utf8Separator;
			fsAccess->local2path(&fsAccess->localseparator, &utf8Separator);
			sBasePath.append(utf8Separator);
		}
		dbAccess = new MegaDbAccess(&sBasePath);
	}
	else dbAccess = NULL;

	gfxAccess = NULL;
	if(processor)
	{
		GfxProcExternal *externalGfx = new GfxProcExternal();
		externalGfx->setProcessor(processor);
		gfxAccess = externalGfx;
	}
	else
	{
		gfxAccess = new MegaGfxProc();
	}

	if(!userAgent)
	{
		userAgent = "";
	}

    client = new MegaClient(this, waiter, httpio, fsAccess, dbAccess, gfxAccess, appKey, userAgent);

#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    httpio->unlock();
#endif

    //Start blocking thread
	threadExit = 0;
    thread.start(threadEntryPoint, this);
}

MegaApiImpl::~MegaApiImpl()
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_DELETE);
    requestQueue.push(request);
    waiter->notify();
    thread.join();
}

int MegaApiImpl::isLoggedIn()
{
    sdkMutex.lock();
    int result = client->loggedin();
    sdkMutex.unlock();
	return result;
}

char* MegaApiImpl::getMyEmail()
{
	User* u;
    sdkMutex.lock();
	if (!client->loggedin() || !(u = client->finduser(client->me)))
	{
		sdkMutex.unlock();
		return NULL;
	}

    char *result = MegaApi::strdup(u->email.c_str());
    sdkMutex.unlock();
    return result;
}

char *MegaApiImpl::getMyUserHandle()
{
    sdkMutex.lock();
    if (ISUNDEF(client->me))
    {
        sdkMutex.unlock();
        return NULL;
    }

    char buf[12];
    Base64::btoa((const byte*)&client->me, MegaClient::USERHANDLE, buf);
    char *result = MegaApi::strdup(buf);
    sdkMutex.unlock();
    return result;
}

MegaUser *MegaApiImpl::getMyUser()
{
    sdkMutex.lock();
    MegaUser *user = MegaUserPrivate::fromUser(client->finduser(client->me));
    sdkMutex.unlock();
    return user;
}

char *MegaApiImpl::getMyXMPPJid()
{
    sdkMutex.lock();
    if (ISUNDEF(client->me))
    {
        sdkMutex.unlock();
        return NULL;
    }

    char jid[16];
    Base32::btoa((const byte *)&client->me, MegaClient::USERHANDLE, jid);
    char *result = MegaApi::strdup(jid);

    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::setLogLevel(int logLevel)
{
    if(!externalLogger)
    {
        externalLogger = new ExternalLogger();
    }
    externalLogger->setLogLevel(logLevel);
}

void MegaApiImpl::setLoggerClass(MegaLogger *megaLogger)
{
    if(!externalLogger)
    {
        externalLogger = new ExternalLogger();
    }
    externalLogger->setMegaLogger(megaLogger);
}

void MegaApiImpl::log(int logLevel, const char *message, const char *filename, int line)
{
    if(!externalLogger)
    {
        return;
    }

    externalLogger->postLog(logLevel, message, filename, line);
}

char* MegaApiImpl::getBase64PwKey(const char *password)
{
	if(!password) return NULL;

	byte pwkey[SymmCipher::KEYLENGTH];
	error e = client->pw_key(password,pwkey);
	if(e)
		return NULL;

	char* buf = new char[SymmCipher::KEYLENGTH*4/3+4];
	Base64::btoa((byte *)pwkey, SymmCipher::KEYLENGTH, buf);
	return buf;
}

char* MegaApiImpl::getStringHash(const char* base64pwkey, const char* inBuf)
{
	if(!base64pwkey || !inBuf) return NULL;

	char pwkey[SymmCipher::KEYLENGTH];
	Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey);

	SymmCipher key;
	key.setkey((byte*)pwkey);

    uint64_t strhash;
	string neBuf = inBuf;

    strhash = client->stringhash64(&neBuf, &key);

	char* buf = new char[8*4/3+4];
    Base64::btoa((byte*)&strhash, 8, buf);
    return buf;
}

void MegaApiImpl::getSessionTransferURL(const char *path, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_SESSION_TRANSFER_URL);
    request->setText(path);
    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

MegaHandle MegaApiImpl::base32ToHandle(const char *base32Handle)
{
	if(!base32Handle) return INVALID_HANDLE;

	handle h = 0;
	Base32::atob(base32Handle,(byte*)&h, MegaClient::USERHANDLE);
	return h;
}

const char* MegaApiImpl::ebcEncryptKey(const char* encryptionKey, const char* plainKey)
{
	if(!encryptionKey || !plainKey) return NULL;

	char pwkey[SymmCipher::KEYLENGTH];
	Base64::atob(encryptionKey, (byte *)pwkey, sizeof pwkey);

	SymmCipher key;
	key.setkey((byte*)pwkey);

	char plkey[SymmCipher::KEYLENGTH];
	Base64::atob(plainKey, (byte*)plkey, sizeof plkey);
	key.ecb_encrypt((byte*)plkey);

	char* buf = new char[SymmCipher::KEYLENGTH*4/3+4];
	Base64::btoa((byte*)plkey, SymmCipher::KEYLENGTH, buf);
	return buf;
}

handle MegaApiImpl::base64ToHandle(const char* base64Handle)
{
	if(!base64Handle) return UNDEF;

	handle h = 0;
	Base64::atob(base64Handle,(byte*)&h,MegaClient::NODEHANDLE);
    return h;
}

char *MegaApiImpl::handleToBase64(MegaHandle handle)
{
    char *base64Handle = new char[12];
    Base64::btoa((byte*)&(handle),MegaClient::NODEHANDLE,base64Handle);
    return base64Handle;
}

char *MegaApiImpl::userHandleToBase64(MegaHandle handle)
{
    char *base64Handle = new char[14];
    Base64::btoa((byte*)&(handle),MegaClient::USERHANDLE,base64Handle);
    return base64Handle;
}

void MegaApiImpl::retryPendingConnections(bool disconnect, bool includexfers, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS);
	request->setFlag(disconnect);
	request->setNumber(includexfers);
	request->setListener(listener);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::addEntropy(char *data, unsigned int size)
{
    if(PrnGen::rng.CanIncorporateEntropy())
        PrnGen::rng.IncorporateEntropy((const byte*)data, size);

#ifdef USE_SODIUM
    if(EdDSA::rng.CanIncorporateEntropy())
        EdDSA::rng.IncorporateEntropy((const byte*)data, size);
#endif

#if (!defined(_WIN32) && !defined(USE_CURL_PUBLIC_KEY_PINNING)) || defined(WINDOWS_PHONE)
    RAND_seed(data, size);
#endif
}

void MegaApiImpl::setStatsID(const char *id)
{
    if (!id || !*id || MegaClient::statsid)
    {
        return;
    }

    MegaClient::statsid = MegaApi::strdup(id);
}

void MegaApiImpl::fastLogin(const char* email, const char *stringHash, const char *base64pwkey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
	request->setEmail(email);
	request->setPassword(stringHash);
	request->setPrivateKey(base64pwkey);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastLogin(const char *session, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
    request->setSessionKey(session);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::killSession(MegaHandle sessionHandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_KILL_SESSION, listener);
    request->setNodeHandle(sessionHandle);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserData(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_USER_DATA, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserData(MegaUser *user, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_USER_DATA, listener);
    request->setFlag(true);
    if(user)
    {
        request->setEmail(user->getEmail());
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserData(const char *user, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_USER_DATA, listener);
    request->setFlag(true);
    request->setEmail(user);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::login(const char *login, const char *password, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
	request->setEmail(login);
	request->setPassword(password);
	requestQueue.push(request);
    waiter->notify();
}

char *MegaApiImpl::dumpSession()
{
    sdkMutex.lock();
    byte session[MAX_SESSION_LENGTH];
    char* buf = NULL;
    int size;
    size = client->dumpsession(session, sizeof session);
    if (size > 0)
    {
        buf = new char[sizeof(session) * 4 / 3 + 4];
        Base64::btoa(session, size, buf);
    }

    sdkMutex.unlock();
    return buf;
}

char *MegaApiImpl::dumpXMPPSession()
{
    sdkMutex.lock();
    char* buf = NULL;

    if (client->loggedin())
    {
        buf = new char[MAX_SESSION_LENGTH * 4 / 3 + 4];
        Base64::btoa((const byte *)client->sid.data(), client->sid.size(), buf);
    }

    sdkMutex.unlock();
    return buf;
}

char *MegaApiImpl::getAccountAuth()
{
    sdkMutex.lock();
    char* buf = NULL;

    if (client->loggedin())
    {
        buf = new char[MAX_SESSION_LENGTH * 4 / 3 + 4];
        Base64::btoa((const byte *)client->sid.data(), client->sid.size(), buf);
    }

    sdkMutex.unlock();
    return buf;
}

void MegaApiImpl::setAccountAuth(const char *auth)
{
    sdkMutex.lock();
    if (!auth)
    {
        client->accountauth.clear();
    }
    else
    {
        client->accountauth = auth;
    }

    handle h = client->getrootpublicfolder();
    if (h != UNDEF)
    {
        client->setrootnode(h);
    }
    sdkMutex.unlock();
}

void MegaApiImpl::createAccount(const char* email, const char* password, const char* name, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_ACCOUNT, listener);
	request->setEmail(email);
	request->setPassword(password);
	request->setName(name);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::createAccount(const char* email, const char* password, const char* firstname, const char* lastname, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_ACCOUNT, listener);
    request->setEmail(email);
    request->setPassword(password);
    request->setName(firstname);
    request->setText(lastname);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastCreateAccount(const char* email, const char *base64pwkey, const char* name, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_ACCOUNT, listener);
	request->setEmail(email);
	request->setPrivateKey(base64pwkey);
	request->setName(name);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::querySignupLink(const char* link, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_QUERY_SIGNUP_LINK, listener);
	request->setLink(link);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::confirmAccount(const char* link, const char *password, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_ACCOUNT, listener);
	request->setLink(link);
	request->setPassword(password);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fastConfirmAccount(const char* link, const char *base64pwkey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_ACCOUNT, listener);
	request->setLink(link);
	request->setPrivateKey(base64pwkey);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::resetPassword(const char *email, bool hasMasterKey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_RECOVERY_LINK, listener);
    request->setEmail(email);
    request->setFlag(hasMasterKey);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::queryRecoveryLink(const char *link, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_QUERY_RECOVERY_LINK, listener);
    request->setLink(link);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::confirmResetPasswordLink(const char *link, const char *newPwd, const char *masterKey, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_RECOVERY_LINK, listener);
    request->setLink(link);
    request->setPassword(newPwd);
    request->setPrivateKey(masterKey);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelAccount(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_CANCEL_LINK, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::confirmCancelAccount(const char *link, const char *pwd, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_CANCEL_LINK, listener);
    request->setLink(link);
    request->setPassword(pwd);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::changeEmail(const char *email, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK, listener);
    request->setEmail(email);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::confirmChangeEmail(const char *link, const char *pwd, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK, listener);
    request->setLink(link);
    request->setPassword(pwd);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setProxySettings(MegaProxy *proxySettings)
{
    Proxy *localProxySettings = new Proxy();
    localProxySettings->setProxyType(proxySettings->getProxyType());

    string url;
    if(proxySettings->getProxyURL())
        url = proxySettings->getProxyURL();

    string localurl;

#if defined(WINDOWS_PHONE) || (defined(_WIN32) && defined(USE_CURL))
    localurl = url;
#else
    fsAccess->path2local(&url, &localurl);
#endif

    localProxySettings->setProxyURL(&localurl);

    if(proxySettings->credentialsNeeded())
    {
        string username;
        if(proxySettings->getUsername())
            username = proxySettings->getUsername();

        string localusername;

#if defined(WINDOWS_PHONE) || (defined(_WIN32) && defined(USE_CURL))
        localusername = username;
#else
        fsAccess->path2local(&username, &localusername);
#endif

        string password;
        if(proxySettings->getPassword())
            password = proxySettings->getPassword();

        string localpassword;

#if defined(WINDOWS_PHONE) || (defined(_WIN32) && defined(USE_CURL))
        localpassword = password;
#else
        fsAccess->path2local(&password, &localpassword);
#endif

        localProxySettings->setCredentials(&localusername, &localpassword);
    }

    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_PROXY);
    request->setProxy(localProxySettings);
    requestQueue.push(request);
    waiter->notify();
}

MegaProxy *MegaApiImpl::getAutoProxySettings()
{
    MegaProxy *proxySettings = new MegaProxy;
    sdkMutex.lock();
    Proxy *localProxySettings = httpio->getautoproxy();
    sdkMutex.unlock();
    proxySettings->setProxyType(localProxySettings->getProxyType());
    if(localProxySettings->getProxyType() == Proxy::CUSTOM)
    {
        string localProxyURL = localProxySettings->getProxyURL();
        string proxyURL;
        fsAccess->local2path(&localProxyURL, &proxyURL);
        LOG_debug << "Autodetected proxy: " << proxyURL;
        proxySettings->setProxyURL(proxyURL.c_str());
    }

    delete localProxySettings;
    return proxySettings;
}

void MegaApiImpl::loop()
{
#if defined(WINDOWS_PHONE) || TARGET_OS_IPHONE
    // Workaround to get the IP of valid DNS servers on Windows Phone/iOS
    string servers;

    while (true)
    {
    #ifdef WINDOWS_PHONE
        client->httpio->getMEGADNSservers(&servers);
    #else
        __res_state res;
        if(res_ninit(&res) == 0)
        {
            union res_sockaddr_union u[MAXNS];
            int nscount = res_getservers(&res, u, MAXNS);

            for(int i = 0; i < nscount; i++)
            {
                char straddr[INET6_ADDRSTRLEN];
                straddr[0] = 0;

                if(u[i].sin.sin_family == PF_INET)
                {
                    mega_inet_ntop(PF_INET, &u[i].sin.sin_addr, straddr, sizeof(straddr));
                }

                if(u[i].sin6.sin6_family == PF_INET6)
                {
                    mega_inet_ntop(PF_INET6, &u[i].sin6.sin6_addr, straddr, sizeof(straddr));
                }

                if(straddr[0])
                {
                    if (servers.size())
                    {
                        servers.append(",");
                    }
                    servers.append(straddr);
                }
            }

            res_ndestroy(&res);
        }
    #endif

        if (servers.size())
            break;

    #ifdef WINDOWS_PHONE
        std::this_thread::sleep_for(std::chrono::seconds(1));
    #else
        sleep(1);
    #endif
    }

    LOG_debug << "Using MEGA DNS servers " << servers;
    httpio->setdnsservers(servers.c_str());

#elif _WIN32
    httpio->lock();
#endif

    while(true)
	{
        sdkMutex.lock();
        int r = client->preparewait();
        sdkMutex.unlock();
        if (!r)
        {
            r = client->dowait();
        }

        if (r & Waiter::NEEDEXEC)
        {
            sendPendingTransfers();
            sendPendingRequests();
            if(threadExit)
                break;

            sdkMutex.lock();
            client->exec();
            sdkMutex.unlock();
        }
	}

    sdkMutex.lock();
    delete client;

	//It doesn't seem fully safe to delete those objects :-/
    // delete httpio;
    // delete waiter;
    // delete fsAccess;
    sdkMutex.unlock();
}


void MegaApiImpl::createFolder(const char *name, MegaNode *parent, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREATE_FOLDER, listener);
    if(parent) request->setParentHandle(parent->getHandle());
	request->setName(name);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::moveNode(MegaNode *node, MegaNode *newParent, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_MOVE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    if(newParent) request->setParentHandle(newParent->getHandle());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::copyNode(MegaNode *node, MegaNode* target, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_COPY, listener);
    if (node)
    {
        request->setPublicNode(node);
        request->setNodeHandle(node->getHandle());
    }
    if(target) request->setParentHandle(target->getHandle());
	requestQueue.push(request);
	waiter->notify();
}

void MegaApiImpl::copyNode(MegaNode *node, MegaNode *target, const char *newName, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_COPY, listener);
    if (node)
    {
        request->setPublicNode(node);
        request->setNodeHandle(node->getHandle());
    }
    if(target) request->setParentHandle(target->getHandle());
    request->setName(newName);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::renameNode(MegaNode *node, const char *newName, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_RENAME, listener);
    if(node) request->setNodeHandle(node->getHandle());
	request->setName(newName);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::remove(MegaNode *node, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE, listener);
    if(node) request->setNodeHandle(node->getHandle());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cleanRubbishBin(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CLEAN_RUBBISH_BIN, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendFileToUser(MegaNode *node, MegaUser *user, MegaRequestListener *listener)
{
	return sendFileToUser(node, user ? user->getEmail() : NULL, listener);
}

void MegaApiImpl::sendFileToUser(MegaNode *node, const char* email, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_COPY, listener);
    if (node)
    {
        request->setPublicNode(node);
        request->setNodeHandle(node->getHandle());
    }
    request->setEmail(email);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::share(MegaNode* node, MegaUser *user, int access, MegaRequestListener *listener)
{
    return share(node, user ? user->getEmail() : NULL, access, listener);
}

void MegaApiImpl::share(MegaNode *node, const char* email, int access, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SHARE, listener);
    if(node) request->setNodeHandle(node->getHandle());
	request->setEmail(email);
	request->setAccess(access);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::loginToFolder(const char* megaFolderLink, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGIN, listener);
	request->setLink(megaFolderLink);
    request->setEmail("FOLDER");
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::importFileLink(const char* megaFileLink, MegaNode *parent, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_IMPORT_LINK, listener);
	if(parent) request->setParentHandle(parent->getHandle());
	request->setLink(megaFileLink);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPublicNode(const char* megaFileLink, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PUBLIC_NODE, listener);
	request->setLink(megaFileLink);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
	getNodeAttribute(node, 0, dstFilePath, listener);
}

void MegaApiImpl::cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener)
{
	cancelGetNodeAttribute(node, 0, listener);
}

void MegaApiImpl::setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
	setNodeAttribute(node, 0, srcFilePath, listener);
}

void MegaApiImpl::getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener)
{
	getNodeAttribute(node, 1, dstFilePath, listener);
}

void MegaApiImpl::cancelGetPreview(MegaNode* node, MegaRequestListener *listener)
{
	cancelGetNodeAttribute(node, 1, listener);
}

void MegaApiImpl::setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener)
{
	setNodeAttribute(node, 1, srcFilePath, listener);
}

void MegaApiImpl::getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener)
{
    const char *email = NULL;
    if (user)
    {
        email = user->getEmail();
    }
    getUserAttr(email, MegaApi::USER_ATTR_AVATAR, dstFilePath, listener);
}

void MegaApiImpl::getUserAvatar(const char* email_or_handle, const char *dstFilePath, MegaRequestListener *listener)
{
    getUserAttr(email_or_handle, MegaApi::USER_ATTR_AVATAR, dstFilePath, listener);
}

void MegaApiImpl::setAvatar(const char *dstFilePath, MegaRequestListener *listener)
{
    setUserAttr(MegaApi::USER_ATTR_AVATAR, dstFilePath, listener);
}

void MegaApiImpl::getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener)
{
    const char *email = NULL;
    if (user)
    {
        email = user->getEmail();
    }
    getUserAttr(email, type ? type : -1, NULL, listener);
}

void MegaApiImpl::getUserAttribute(const char* email_or_handle, int type, MegaRequestListener *listener)
{
    getUserAttr(email_or_handle, type ? type : -1, NULL, listener);
}

void MegaApiImpl::setUserAttribute(int type, const char *value, MegaRequestListener *listener)
{
    setUserAttr(type ? type : -1, value, listener);
}

void MegaApiImpl::setCustomNodeAttribute(MegaNode *node, const char *attrName, const char *value, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_NODE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setName(attrName);
    request->setText(value);
    request->setFlag(false);     // is official attribute?
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setNodeDuration(MegaNode *node, int secs, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_NODE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setParamType(MegaApi::NODE_ATTR_DURATION);
    request->setNumber(secs);
    request->setFlag(true);     // is official attribute?
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setNodeCoordinates(MegaNode *node, double latitude, double longitude, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_NODE, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setParamType(MegaApi::NODE_ATTR_COORDINATES);

    // calculate the required size of the string containing both values
    size_t size = 0;
    std::ostringstream strs;
    strs << (int) latitude << (int) longitude;
    size = strs.str().length() + 1 + 7 * 2 + 1;

    char coords[size];     // <lat>;<lon>
    snprintf(coords, size, "%.6f;%.6f", latitude, longitude);
    request->setText(coords);
    request->setFlag(true);     // is official attribute?
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::exportNode(MegaNode *node, int64_t expireTime, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_EXPORT, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setNumber(expireTime);
    request->setAccess(1);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::disableExport(MegaNode *node, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_EXPORT, listener);
    if(node) request->setNodeHandle(node->getHandle());
    request->setAccess(0);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fetchNodes(MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_FETCH_NODES, listener);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPricing(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PRICING, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPaymentId(handle productHandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PAYMENT_ID, listener);
    request->setNodeHandle(productHandle);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::upgradeAccount(MegaHandle productHandle, int paymentMethod, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_UPGRADE_ACCOUNT, listener);
    request->setNodeHandle(productHandle);
    request->setNumber(paymentMethod);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::submitPurchaseReceipt(int gateway, const char *receipt, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT, listener);
    request->setNumber(gateway);
    request->setText(receipt);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::creditCardStore(const char* address1, const char* address2, const char* city,
                                  const char* province, const char* country, const char *postalcode,
                                  const char* firstname, const char* lastname, const char* creditcard,
                                  const char* expire_month, const char* expire_year, const char* cv2,
                                  MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREDIT_CARD_STORE, listener);
    string email;

    sdkMutex.lock();
    User *u = client->finduser(client->me);
    if (u)
    {
        email = u->email;
    }
    sdkMutex.unlock();

    if (email.size())
    {
        string saddress1, saddress2, scity, sprovince, scountry, spostalcode;
        string sfirstname, slastname, screditcard, sexpire_month, sexpire_year, scv2;

        if (address1)
        {
           saddress1 = address1;
        }

        if (address2)
        {
            saddress2 = address2;
        }

        if (city)
        {
            scity = city;
        }

        if (province)
        {
            sprovince = province;
        }

        if (country)
        {
            scountry = country;
        }

        if (postalcode)
        {
            spostalcode = postalcode;
        }

        if (firstname)
        {
            sfirstname = firstname;
        }

        if (lastname)
        {
            slastname = lastname;
        }

        if (creditcard)
        {
            screditcard = creditcard;
            screditcard.erase(remove_if(screditcard.begin(), screditcard.end(),
                                     not1(ptr_fun(static_cast<int(*)(int)>(isdigit)))), screditcard.end());
        }

        if (expire_month)
        {
            sexpire_month = expire_month;
        }

        if (expire_year)
        {
            sexpire_year = expire_year;
        }

        if (cv2)
        {
            scv2 = cv2;
        }

        int tam = 256 + sfirstname.size() + slastname.size() + screditcard.size()
                + sexpire_month.size() + sexpire_year.size() + scv2.size() + saddress1.size()
                + saddress2.size() + scity.size() + sprovince.size() + spostalcode.size()
                + scountry.size() + email.size();

        char *ccplain = new char[tam];
        snprintf(ccplain, tam, "{\"first_name\":\"%s\",\"last_name\":\"%s\","
                "\"card_number\":\"%s\","
                "\"expiry_date_month\":\"%s\",\"expiry_date_year\":\"%s\","
                "\"cv2\":\"%s\",\"address1\":\"%s\","
                "\"address2\":\"%s\",\"city\":\"%s\","
                "\"province\":\"%s\",\"postal_code\":\"%s\","
                "\"country_code\":\"%s\",\"email_address\":\"%s\"}", sfirstname.c_str(), slastname.c_str(),
                 screditcard.c_str(), sexpire_month.c_str(), sexpire_year.c_str(), scv2.c_str(), saddress1.c_str(),
                 saddress2.c_str(), scity.c_str(), sprovince.c_str(), spostalcode.c_str(), scountry.c_str(), email.c_str());

        request->setText((const char* )ccplain);
        delete [] ccplain;
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::creditCardQuerySubscriptions(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::creditCardCancelSubscriptions(const char* reason, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS, listener);
    request->setText(reason);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getPaymentMethods(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_PAYMENT_METHODS, listener);
    requestQueue.push(request);
    waiter->notify();
}

char *MegaApiImpl::exportMasterKey()
{
    sdkMutex.lock();
    char* buf = NULL;

    if(client->loggedin())
    {
        buf = new char[SymmCipher::KEYLENGTH * 4 / 3 + 4];
        Base64::btoa(client->key.key, SymmCipher::KEYLENGTH, buf);
    }

    sdkMutex.unlock();
    return buf;
}

void MegaApiImpl::getAccountDetails(bool storage, bool transfer, bool pro, bool sessions, bool purchases, bool transactions, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ACCOUNT_DETAILS, listener);
	int numDetails = 0;
	if(storage) numDetails |= 0x01;
    if(transfer) numDetails |= 0x02;
	if(pro) numDetails |= 0x04;
	if(transactions) numDetails |= 0x08;
	if(purchases) numDetails |= 0x10;
	if(sessions) numDetails |= 0x20;
	request->setNumDetails(numDetails);

	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHANGE_PW, listener);
	request->setPassword(oldPassword);
	request->setNewPassword(newPassword);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::logout(MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT, listener);
    request->setFlag(true);
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::localLogout(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT, listener);
    request->setFlag(false);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::submitFeedback(int rating, const char *comment, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SUBMIT_FEEDBACK, listener);
    request->setText(comment);
    request->setNumber(rating);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::reportEvent(const char *details, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REPORT_EVENT, listener);
    request->setText(details);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::sendEvent(int eventType, const char *message, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SEND_EVENT, listener);
    request->setNumber(eventType);
    request->setText(message);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::useHttpsOnly(bool usehttps)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_USE_HTTPS_ONLY);
    request->setFlag(usehttps);
    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::usingHttpsOnly()
{
    return client->usehttps;
}

void MegaApiImpl::getNodeAttribute(MegaNode *node, int type, const char *dstFilePath, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_FILE, listener);
    if(dstFilePath)
    {
        string path(dstFilePath);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif

        int c = path[path.size()-1];
        if((c=='/') || (c == '\\'))
        {
            const char *base64Handle = node->getBase64Handle();
            path.append(base64Handle);
            path.push_back('0' + type);
            path.append(".jpg");
            delete [] base64Handle;
        }

        request->setFile(path.c_str());
    }

    request->setParamType(type);
    if(node) request->setNodeHandle(node->getHandle());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelGetNodeAttribute(MegaNode *node, int type, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_ATTR_FILE, listener);
	request->setParamType(type);
	if (node) request->setNodeHandle(node->getHandle());
	requestQueue.push(request);
	waiter->notify();
}

void MegaApiImpl::setNodeAttribute(MegaNode *node, int type, const char *srcFilePath, MegaRequestListener *listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_FILE, listener);
	request->setFile(srcFilePath);
    request->setParamType(type);
    if(node) request->setNodeHandle(node->getHandle());
	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUserAttr(const char *email_or_handle, int type, const char *dstFilePath, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_GET_ATTR_USER, listener);

    if (type == MegaApi::USER_ATTR_AVATAR && dstFilePath)
    {
        string path(dstFilePath);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif

        int c = path[path.size()-1];
        if((c=='/') || (c == '\\'))
        {
            path.append(email_or_handle);
            path.push_back('0' + type);
            path.append(".jpg");
        }

        request->setFile(path.c_str());
    }

    request->setParamType(type);
    if(email_or_handle)
    {
        request->setEmail(email_or_handle);
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::setUserAttr(int type, const char *value, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_SET_ATTR_USER, listener);
    if(type == MegaApi::USER_ATTR_AVATAR)
    {
        request->setFile(value);
    }
    else
    {
        request->setText(value);
    }

    request->setParamType(type);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::inviteContact(const char *email, const char *message,int action, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_INVITE_CONTACT, listener);
    request->setNumber(action);
    request->setEmail(email);
    request->setText(message);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::replyContactRequest(MegaContactRequest *r, int action, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REPLY_CONTACT_REQUEST, listener);
    if(r)
    {
        request->setNodeHandle(r->getHandle());
    }

    request->setNumber(action);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::removeContact(MegaUser *user, MegaRequestListener* listener)
{
	MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_CONTACT, listener);
    if(user)
    {
        request->setEmail(user->getEmail());
    }

	requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::pauseTransfers(bool pause, int direction, MegaRequestListener* listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_PAUSE_TRANSFERS, listener);
    request->setFlag(pause);
    request->setNumber(direction);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::enableTransferResumption(const char *loggedOutId)
{
    sdkMutex.lock();
    client->enabletransferresumption(loggedOutId);
    sdkMutex.unlock();
}

void MegaApiImpl::disableTransferResumption(const char *loggedOutId)
{
    sdkMutex.lock();
    client->disabletransferresumption(loggedOutId);
    sdkMutex.unlock();
}

bool MegaApiImpl::areTransfersPaused(int direction)
{
    if(direction != MegaTransfer::TYPE_DOWNLOAD && direction != MegaTransfer::TYPE_UPLOAD)
    {
        return false;
    }

    bool result;
    sdkMutex.lock();
    if(direction == MegaTransfer::TYPE_DOWNLOAD)
    {
        result = client->xferpaused[GET];
    }
    else
    {
        result = client->xferpaused[PUT];
    }
    sdkMutex.unlock();
    return result;
}

//-1 -> AUTO, 0 -> NONE, >0 -> b/s
void MegaApiImpl::setUploadLimit(int bpslimit)
{
    client->putmbpscap = bpslimit;
}

void MegaApiImpl::setDownloadMethod(int method)
{
    switch(method)
    {
        case MegaApi::TRANSFER_METHOD_NORMAL:
            client->usealtdownport = false;
            client->autodownport = false;
            break;
        case MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT:
            client->usealtdownport = true;
            client->autodownport = false;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO:
            client->autodownport = true;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO_NORMAL:
            client->usealtdownport = false;
            client->autodownport = true;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO_ALTERNATIVE:
            client->usealtdownport = true;
            client->autodownport = true;
            break;
        default:
            break;
    }
}

void MegaApiImpl::setUploadMethod(int method)
{
    switch(method)
    {
        case MegaApi::TRANSFER_METHOD_NORMAL:
            client->usealtupport = false;
            client->autoupport = false;
            break;
        case MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT:
            client->usealtupport = true;
            client->autoupport = false;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO:
            client->autoupport = true;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO_NORMAL:
            client->usealtupport = false;
            client->autoupport = true;
            break;
        case MegaApi::TRANSFER_METHOD_AUTO_ALTERNATIVE:
            client->usealtupport = true;
            client->autoupport = true;
            break;
        default:
            break;
    }
}

int MegaApiImpl::getDownloadMethod()
{
    if (client->autodownport)
    {
        if(client->usealtdownport)
        {
            return MegaApi::TRANSFER_METHOD_AUTO_ALTERNATIVE;
        }
        else
        {
            return MegaApi::TRANSFER_METHOD_AUTO_NORMAL;
        }
    }

    if (client->usealtdownport)
    {
        return MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT;
    }

    return MegaApi::TRANSFER_METHOD_NORMAL;
}

int MegaApiImpl::getUploadMethod()
{
    if (client->autoupport)
    {
        if(client->usealtupport)
        {
            return MegaApi::TRANSFER_METHOD_AUTO_ALTERNATIVE;
        }
        else
        {
            return MegaApi::TRANSFER_METHOD_AUTO_NORMAL;
        }
    }

    if (client->usealtupport)
    {
        return MegaApi::TRANSFER_METHOD_ALTERNATIVE_PORT;
    }

    return MegaApi::TRANSFER_METHOD_NORMAL;
}

MegaTransferList *MegaApiImpl::getTransfers()
{
    sdkMutex.lock();

    vector<MegaTransfer *> transfers;
    for (int d = GET; d == GET || d == PUT; d += PUT - GET)
    {
        for (transfer_map::iterator it = client->transfers[d].begin(); it != client->transfers[d].end(); it++)
        {
            Transfer *t = it->second;
            if(transferMap.find(t->tag) == transferMap.end())
            {
                continue;
            }
            MegaTransferPrivate* transfer = transferMap.at(t->tag);
            transfers.push_back(transfer);
        }
    }

    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), transfers.size());

    sdkMutex.unlock();
    return result;
}

MegaTransferList *MegaApiImpl::getStreamingTransfers()
{
    sdkMutex.lock();

    vector<MegaTransfer *> transfers;
    for (std::map<int, MegaTransferPrivate *>::iterator it = transferMap.begin(); it != transferMap.end(); it++)
    {
        MegaTransferPrivate *transfer = it->second;
        if (transfer->isStreamingTransfer())
        {
            transfers.push_back(transfer);
        }
    }
    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), transfers.size());

    sdkMutex.unlock();
    return result;
}

MegaTransfer *MegaApiImpl::getTransferByTag(int transferTag)
{
    MegaTransfer* value = NULL;
    sdkMutex.lock();

    if(transferMap.find(transferTag) == transferMap.end())
    {
        sdkMutex.unlock();
        return NULL;
    }

    value = transferMap.at(transferTag)->copy();
    sdkMutex.unlock();
    return value;
}

MegaTransferList *MegaApiImpl::getTransfers(int type)
{
    if(type != MegaTransfer::TYPE_DOWNLOAD && type != MegaTransfer::TYPE_UPLOAD)
    {
        return new MegaTransferListPrivate();
    }

    sdkMutex.lock();

    vector<MegaTransfer *> transfers;
    for (transfer_map::iterator it = client->transfers[type].begin(); it != client->transfers[type].end(); it++)
    {
        Transfer *t = it->second;
        if(transferMap.find(t->tag) == transferMap.end())
        {
            continue;
        }
        MegaTransferPrivate* transfer = transferMap.at(t->tag);
        transfers.push_back(transfer);
    }

    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), transfers.size());

    sdkMutex.unlock();
    return result;
}

MegaTransferList *MegaApiImpl::getChildTransfers(int transferTag)
{
    sdkMutex.lock();

    if(transferMap.find(transferTag) == transferMap.end())
    {
        sdkMutex.unlock();
        return new MegaTransferListPrivate();
    }

    MegaTransfer *transfer = transferMap.at(transferTag);
    if(!transfer->isFolderTransfer())
    {
        sdkMutex.unlock();
        return new MegaTransferListPrivate();
    }

    vector<MegaTransfer *> transfers;
    for(std::map<int, MegaTransferPrivate *>::iterator it = transferMap.begin(); it != transferMap.end(); it++)
    {
        MegaTransferPrivate *t = it->second;
        if(t->getFolderTransferTag() == transferTag)
        {
            transfers.push_back(transfer);
        }
    }

    MegaTransferList *result = new MegaTransferListPrivate(transfers.data(), transfers.size());

    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::startUpload(const char *localPath, MegaNode *parent, const char *fileName, int64_t mtime, int folderTransferTag, MegaTransferListener *listener)
{
    MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_UPLOAD, listener);
    if(localPath)
    {
        string path(localPath);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif
        transfer->setPath(path.data());
    }

    if(parent)
    {
        transfer->setParentHandle(parent->getHandle());
    }

    transfer->setMaxRetries(maxRetries);

    if(fileName)
    {
        transfer->setFileName(fileName);
    }

    transfer->setTime(mtime);

    if(folderTransferTag)
    {
        transfer->setFolderTransferTag(folderTransferTag);
    }

	transferQueue.push(transfer);
    waiter->notify();
}

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, MegaTransferListener *listener)
{ return startUpload(localPath, parent, (const char *)NULL, -1, 0, listener); }

void MegaApiImpl::startUpload(const char *localPath, MegaNode *parent, int64_t mtime, MegaTransferListener *listener)
{ return startUpload(localPath, parent, (const char *)NULL, mtime, 0, listener); }

void MegaApiImpl::startUpload(const char* localPath, MegaNode* parent, const char* fileName, MegaTransferListener *listener)
{ return startUpload(localPath, parent, fileName, -1, 0, listener); }

void MegaApiImpl::startDownload(MegaNode *node, const char* localPath, long /*startPos*/, long /*endPos*/, int folderTransferTag, const char *appData, MegaTransferListener *listener)
{
	MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_DOWNLOAD, listener);

    if(localPath)
    {
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        string path(localPath);
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
        localPath = path.data();
#endif

        int c = localPath[strlen(localPath)-1];
        if((c=='/') || (c == '\\')) transfer->setParentPath(localPath);
        else transfer->setPath(localPath);
    }

    if(node)
    {
        transfer->setNodeHandle(node->getHandle());
        if (node->isPublic() || node->isForeign())
        {
            transfer->setPublicNode(node);
        }
    }

    transfer->setMaxRetries(maxRetries);
    transfer->setAppData(appData);

    if (folderTransferTag)
    {
        transfer->setFolderTransferTag(folderTransferTag);
    }

	transferQueue.push(transfer);
	waiter->notify();
}

void MegaApiImpl::startDownload(MegaNode *node, const char* localFolder, MegaTransferListener *listener)
{ startDownload(node, localFolder, 0, 0, 0, NULL, listener); }

void MegaApiImpl::cancelTransfer(MegaTransfer *t, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_TRANSFER, listener);
    if(t)
    {
        request->setTransferTag(t->getTag());
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelTransferByTag(int transferTag, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_TRANSFER, listener);
    request->setTransferTag(transferTag);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::cancelTransfers(int direction, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CANCEL_TRANSFERS, listener);
    request->setParamType(direction);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::startStreaming(MegaNode* node, m_off_t startPos, m_off_t size, MegaTransferListener *listener)
{
	MegaTransferPrivate* transfer = new MegaTransferPrivate(MegaTransfer::TYPE_DOWNLOAD, listener);
    if(node && !node->isPublic() && !node->isForeign())
	{
		transfer->setNodeHandle(node->getHandle());
	}
	else
	{
		transfer->setPublicNode(node);
	}

    transfer->setStreamingTransfer(true);
	transfer->setStartPos(startPos);
	transfer->setEndPos(startPos + size - 1);
	transfer->setMaxRetries(maxRetries);
	transferQueue.push(transfer);
	waiter->notify();
}

#ifdef ENABLE_SYNC

//Move local files inside synced folders to the "Rubbish" folder.
bool MegaApiImpl::moveToLocalDebris(const char *path)
{
    sdkMutex.lock();

    string utf8path = path;
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(utf8path.c_str()) && ((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\")))
            utf8path.insert(0, "\\\\?\\");
#endif

    string localpath;
    fsAccess->path2local(&utf8path, &localpath);

    Sync *sync = NULL;
    for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
    {
        string *localroot = &((*it)->localroot.localname);
        if(((localroot->size()+fsAccess->localseparator.size())<localpath.size()) &&
            !memcmp(localroot->data(), localpath.data(), localroot->size()) &&
            !memcmp(fsAccess->localseparator.data(), localpath.data()+localroot->size(), fsAccess->localseparator.size()))
        {
            sync = (*it);
            break;
        }
    }

    if(!sync)
    {
        sdkMutex.unlock();
        return false;
    }

    bool result = sync->movetolocaldebris(&localpath);
    sdkMutex.unlock();

    return result;
}

int MegaApiImpl::syncPathState(string* path)
{
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    string prefix("\\\\?\\");
    string localPrefix;
    fsAccess->path2local(&prefix, &localPrefix);
    path->append("", 1);
    if(!PathIsRelativeW((LPCWSTR)path->data()) && (path->size()<4 || memcmp(path->data(), localPrefix.data(), 4)))
    {
        path->insert(0, localPrefix);
    }
    path->resize(path->size() - 1);
#endif

    int state = MegaApi::STATE_NONE;
    sdkMutex.lock();
    for (sync_list::iterator it = client->syncs.begin(); it != client->syncs.end(); it++)
    {
        Sync *sync = (*it);
        unsigned int ssize = sync->localroot.localname.size();
        if(path->size() < ssize || memcmp(path->data(), sync->localroot.localname.data(), ssize))
            continue;

        if(path->size() == ssize)
        {
            state = sync->localroot.ts;
            break;
        }
        else if(!memcmp(path->data()+ssize, client->fsaccess->localseparator.data(), client->fsaccess->localseparator.size()))
        {
            LocalNode* l = sync->localnodebypath(NULL, path);
            if(l)
                state = l->ts;
            else
                state = MegaApi::STATE_IGNORED;
            break;
        }
    }
    sdkMutex.unlock();
    return state;
}


MegaNode *MegaApiImpl::getSyncedNode(string *path)
{
    sdkMutex.lock();
    MegaNode *node = NULL;
    for (sync_list::iterator it = client->syncs.begin(); (it != client->syncs.end()) && (node == NULL); it++)
    {
        Sync *sync = (*it);
        if(path->size() == sync->localroot.localname.size() &&
                !memcmp(path->data(), sync->localroot.localname.data(), path->size()))
        {
            node = MegaNodePrivate::fromNode(sync->localroot.node);
            break;
        }

        LocalNode * localNode = sync->localnodebypath(NULL, path);
        if(localNode) node = MegaNodePrivate::fromNode(localNode->node);
    }
    sdkMutex.unlock();
    return node;
}

void MegaApiImpl::syncFolder(const char *localFolder, MegaNode *megaFolder, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);
    if(megaFolder) request->setNodeHandle(megaFolder->getHandle());
    if(localFolder)
    {
        string path(localFolder);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif
        request->setFile(path.data());
    }

    request->setListener(listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::resumeSync(const char *localFolder, long long localfp, MegaNode *megaFolder, MegaRequestListener* listener)
{
    sdkMutex.lock();

#ifdef __APPLE__
    localfp = 0;
#endif

    LOG_debug << "Resume sync";

    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);
    request->setListener(listener);
    if(megaFolder) request->setNodeHandle(megaFolder->getHandle());
    if(localFolder)
    {
        string path(localFolder);
#if defined(_WIN32) && !defined(WINDOWS_PHONE)
        if(!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
            path.insert(0, "\\\\?\\");
#endif
        request->setFile(path.data());
    }
    request->setNumber(localfp);

    int nextTag = client->nextreqtag();
    request->setTag(nextTag);
    requestMap[nextTag]=request;
    error e = API_OK;
    fireOnRequestStart(request);

    const char *localPath = request->getFile();
    Node *node = client->nodebyhandle(request->getNodeHandle());
    if(!node || (node->type==FILENODE) || !localPath)
    {
        e = API_EARGS;
    }
    else
    {
        string utf8name(localPath);
        string localname;
        client->fsaccess->path2local(&utf8name, &localname);
        e = client->addsync(&localname, DEBRISFOLDER, NULL, node, localfp, -nextTag);
        if(!e)
        {
            MegaSyncPrivate *sync = new MegaSyncPrivate(client->syncs.back());
            sync->setListener(request->getSyncListener());
            syncMap[-nextTag] = sync;

            request->setNumber(client->syncs.back()->fsfp);
        }
    }

    fireOnRequestFinish(request, MegaError(e));
    sdkMutex.unlock();
}

void MegaApiImpl::removeSync(handle nodehandle, MegaRequestListener* listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_SYNC, listener);
    request->setNodeHandle(nodehandle);
    request->setFlag(true);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::disableSync(handle nodehandle, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_SYNC, listener);
    request->setNodeHandle(nodehandle);
    request->setFlag(false);
    requestQueue.push(request);
    waiter->notify();
}

int MegaApiImpl::getNumActiveSyncs()
{
    sdkMutex.lock();
    int num = client->syncs.size();
    sdkMutex.unlock();
    return num;
}

void MegaApiImpl::stopSyncs(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_REMOVE_SYNCS, listener);
    requestQueue.push(request);
    waiter->notify();
}

bool MegaApiImpl::isSynced(MegaNode *n)
{
    if(!n) return false;
    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return false;
    }

    bool result = (node->localnode!=NULL);
    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::setExcludedNames(vector<string> *excludedNames)
{
    sdkMutex.lock();
    if(!excludedNames)
    {
        this->excludedNames.clear();
        sdkMutex.unlock();
        return;
    }

    for(unsigned int i=0; i<excludedNames->size(); i++)
    {
        LOG_debug << "Excluded name: " << excludedNames->at(i);
    }

    this->excludedNames = *excludedNames;
    sdkMutex.unlock();
}

void MegaApiImpl::setExclusionLowerSizeLimit(long long limit)
{
    syncLowerSizeLimit = limit;
}

void MegaApiImpl::setExclusionUpperSizeLimit(long long limit)
{
    syncUpperSizeLimit = limit;
}

string MegaApiImpl::getLocalPath(MegaNode *n)
{
    if(!n) return string();
    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node || !node->localnode)
    {
        sdkMutex.unlock();
        return string();
    }

    string result;
    node->localnode->getlocalpath(&result, true);
    result.append("", 1);
    sdkMutex.unlock();
    return result;
}

#endif

int MegaApiImpl::getNumPendingUploads()
{
    return pendingUploads;
}

int MegaApiImpl::getNumPendingDownloads()
{
    return pendingDownloads;
}

int MegaApiImpl::getTotalUploads()
{
    return totalUploads;
}

int MegaApiImpl::getTotalDownloads()
{
    return totalDownloads;
}

void MegaApiImpl::resetTotalDownloads()
{
    totalDownloads = 0;
}

void MegaApiImpl::resetTotalUploads()
{
    totalUploads = 0;
}

MegaNode *MegaApiImpl::getRootNode()
{
    sdkMutex.lock();
    MegaNode *result = MegaNodePrivate::fromNode(client->nodebyhandle(client->rootnodes[0]));
    sdkMutex.unlock();
	return result;
}

MegaNode* MegaApiImpl::getInboxNode()
{
    sdkMutex.lock();
    MegaNode *result = MegaNodePrivate::fromNode(client->nodebyhandle(client->rootnodes[1]));
    sdkMutex.unlock();
	return result;
}

MegaNode* MegaApiImpl::getRubbishNode()
{
    sdkMutex.lock();
    MegaNode *result = MegaNodePrivate::fromNode(client->nodebyhandle(client->rootnodes[2]));
    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::setDefaultFilePermissions(int permissions)
{
    fsAccess->setdefaultfilepermissions(permissions);
}

int MegaApiImpl::getDefaultFilePermissions()
{
    return fsAccess->getdefaultfilepermissions();
}

void MegaApiImpl::setDefaultFolderPermissions(int permissions)
{
    fsAccess->setdefaultfolderpermissions(permissions);
}

int MegaApiImpl::getDefaultFolderPermissions()
{
    return fsAccess->getdefaultfolderpermissions();
}

long long MegaApiImpl::getBandwidthOverquotaDelay()
{
    long long result = client->overquotauntil;
    return result > Waiter::ds ? (result - Waiter::ds) / 10 : 0;
}

bool MegaApiImpl::userComparatorDefaultASC (User *i, User *j)
{
	if(strcasecmp(i->email.c_str(), j->email.c_str())<=0) return 1;
    return 0;
}

char *MegaApiImpl::escapeFsIncompatible(const char *filename)
{
    if(!filename)
    {
        return NULL;
    }
    string name = filename;
    client->fsaccess->escapefsincompatible(&name);
    return MegaApi::strdup(name.c_str());
}

char *MegaApiImpl::unescapeFsIncompatible(const char *name)
{
    if(!name)
    {
        return NULL;
    }
    string filename = name;
    client->fsaccess->unescapefsincompatible(&filename);
    return MegaApi::strdup(filename.c_str());
}

bool MegaApiImpl::createThumbnail(const char *imagePath, const char *dstPath)
{
    if (!gfxAccess)
    {
        return false;
    }

    string utf8ImagePath = imagePath;
    string localImagePath;
    fsAccess->path2local(&utf8ImagePath, &localImagePath);

    string utf8DstPath = dstPath;
    string localDstPath;
    fsAccess->path2local(&utf8DstPath, &localDstPath);

    sdkMutex.lock();
    bool result = gfxAccess->savefa(&localImagePath, GfxProc::THUMBNAIL120X120, &localDstPath);
    sdkMutex.unlock();

    return result;
}

bool MegaApiImpl::createPreview(const char *imagePath, const char *dstPath)
{
    if (!gfxAccess)
    {
        return false;
    }

    string utf8ImagePath = imagePath;
    string localImagePath;
    fsAccess->path2local(&utf8ImagePath, &localImagePath);

    string utf8DstPath = dstPath;
    string localDstPath;
    fsAccess->path2local(&utf8DstPath, &localDstPath);

    sdkMutex.lock();
    bool result = gfxAccess->savefa(&localImagePath, GfxProc::PREVIEW1000x1000, &localDstPath);
    sdkMutex.unlock();

    return result;
}

bool MegaApiImpl::isOnline()
{
    return !client->httpio->noinetds;
}

#ifdef HAVE_LIBUV
bool MegaApiImpl::httpServerStart(bool localOnly, int port)
{
    sdkMutex.lock();
    if (httpServer && httpServer->getPort() == port && httpServer->isLocalOnly() == localOnly)
    {
        httpServer->clearAllowedHandles();
        sdkMutex.unlock();
        return true;
    }

    httpServerStop();
    httpServer = new MegaHTTPServer(this);
    httpServer->setMaxBufferSize(httpServerMaxBufferSize);
    httpServer->setMaxOutputSize(httpServerMaxOutputSize);
    httpServer->enableFileServer(httpServerEnableFiles);
    httpServer->enableFolderServer(httpServerEnableFolders);
    httpServer->setRestrictedMode(httpServerRestrictedMode);
    httpServer->enableSubtitlesSupport(httpServerRestrictedMode);

    bool result = httpServer->start(port, localOnly);
    if (!result)
    {
        MegaHTTPServer *server = httpServer;
        httpServer = NULL;
        sdkMutex.unlock();
        delete server;
    }
    else
    {
        sdkMutex.unlock();
    }
    return result;
}

void MegaApiImpl::httpServerStop()
{
    sdkMutex.lock();
    if (httpServer)
    {
        MegaHTTPServer *server = httpServer;
        httpServer = NULL;
        sdkMutex.unlock();
        delete server;
    }
    else
    {
        sdkMutex.unlock();
    }
}

int MegaApiImpl::httpServerIsRunning()
{
    bool result = false;
    sdkMutex.lock();
    if (httpServer)
    {
        result = httpServer->getPort();
    }
    sdkMutex.unlock();
    return result;
}

char *MegaApiImpl::httpServerGetLocalLink(MegaNode *node)
{
    if (!node)
    {
        return NULL;
    }

    sdkMutex.lock();
    if (!httpServer)
    {
        sdkMutex.unlock();
        return NULL;
    }

    char *result = httpServer->getLink(node);
    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::httpServerSetMaxBufferSize(int bufferSize)
{
    sdkMutex.lock();
    httpServerMaxBufferSize = bufferSize <= 0 ? 0 : bufferSize;
    if (httpServer)
    {
        httpServer->setMaxBufferSize(httpServerMaxBufferSize);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::httpServerGetMaxBufferSize()
{
    int value;
    sdkMutex.lock();
    if (httpServerMaxBufferSize)
    {
        value = httpServerMaxBufferSize;
    }
    else
    {
        value = StreamingBuffer::MAX_BUFFER_SIZE;
    }
    sdkMutex.unlock();
    return value;
}

void MegaApiImpl::httpServerSetMaxOutputSize(int outputSize)
{
    sdkMutex.lock();
    httpServerMaxOutputSize = outputSize <= 0 ? 0 : outputSize;
    if (httpServer)
    {
        httpServer->setMaxOutputSize(httpServerMaxOutputSize);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::httpServerGetMaxOutputSize()
{
    int value;
    sdkMutex.lock();
    if (httpServerMaxOutputSize)
    {
        value = httpServerMaxOutputSize;
    }
    else
    {
        value = StreamingBuffer::MAX_OUTPUT_SIZE;
    }
    sdkMutex.unlock();
    return value;
}

void MegaApiImpl::httpServerEnableFileServer(bool enable)
{
    sdkMutex.lock();
    this->httpServerEnableFiles = enable;
    if (httpServer)
    {
        httpServer->enableFileServer(enable);
    }
    sdkMutex.unlock();
}

bool MegaApiImpl::httpServerIsFileServerEnabled()
{
    return httpServerEnableFiles;
}

void MegaApiImpl::httpServerEnableFolderServer(bool enable)
{
    sdkMutex.lock();
    this->httpServerEnableFolders = enable;
    if (httpServer)
    {
        httpServer->enableFolderServer(enable);
    }
    sdkMutex.unlock();
}

bool MegaApiImpl::httpServerIsFolderServerEnabled()
{
    return httpServerEnableFolders;
}

void MegaApiImpl::httpServerSetRestrictedMode(int mode)
{
    if (mode != MegaApi::HTTP_SERVER_DENY_ALL
            && mode != MegaApi::HTTP_SERVER_ALLOW_ALL
            && mode != MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS
            && mode != MegaApi::HTTP_SERVER_ALLOW_LAST_LOCAL_LINK)
    {
        return;
    }

    sdkMutex.lock();
    httpServerRestrictedMode = mode;
    if (httpServer)
    {
        httpServer->setRestrictedMode(httpServerRestrictedMode);
    }
    sdkMutex.unlock();
}

int MegaApiImpl::httpServerGetRestrictedMode()
{
    return httpServerRestrictedMode;
}

void MegaApiImpl::httpServerEnableSubtitlesSupport(bool enable)
{
    sdkMutex.lock();
    httpServerSubtitlesSupportEnabled = enable;
    if (httpServer)
    {
        httpServer->enableSubtitlesSupport(httpServerSubtitlesSupportEnabled);
    }
    sdkMutex.unlock();
}

bool MegaApiImpl::httpServerIsSubtitlesSupportEnabled()
{
    return httpServerSubtitlesSupportEnabled;
}

bool MegaApiImpl::httpServerIsLocalOnly()
{
    bool localOnly = true;
    sdkMutex.lock();
    if (httpServer)
    {
        localOnly = httpServer->isLocalOnly();
    }
    sdkMutex.unlock();
    return localOnly;
}

void MegaApiImpl::httpServerAddListener(MegaTransferListener *listener)
{
    if (!listener)
    {
        return;
    }

    sdkMutex.lock();
    httpServerListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::httpServerRemoveListener(MegaTransferListener *listener)
{
    if (!listener)
    {
        return;
    }

    sdkMutex.lock();
    httpServerListeners.erase(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::fireOnStreamingStart(MegaTransferPrivate *transfer)
{
    for(set<MegaTransferListener *>::iterator it = httpServerListeners.begin(); it != httpServerListeners.end() ; it++)
        (*it)->onTransferStart(api, transfer);
}

void MegaApiImpl::fireOnStreamingTemporaryError(MegaTransferPrivate *transfer, MegaError e)
{
    for(set<MegaTransferListener *>::iterator it = httpServerListeners.begin(); it != httpServerListeners.end() ; it++)
        (*it)->onTransferTemporaryError(api, transfer, &e);
}

void MegaApiImpl::fireOnStreamingFinish(MegaTransferPrivate *transfer, MegaError e)
{
    if(e.getErrorCode())
    {
        LOG_warn << "Streaming request finished with error: " << e.getErrorString();
    }
    else
    {
        LOG_info << "Streaming request finished";
    }

    for(set<MegaTransferListener *>::iterator it = httpServerListeners.begin(); it != httpServerListeners.end() ; it++)
        (*it)->onTransferFinish(api, transfer, &e);

    delete transfer;
}
#endif

#ifdef ENABLE_CHAT
void MegaApiImpl::createChat(bool group, MegaTextChatPeerList *peers, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_CREATE, listener);
    request->setFlag(group);
    request->setMegaTextChatPeerList(peers);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::fetchChats(MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_FETCH, listener);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::inviteToChat(MegaHandle chatid, MegaHandle uh, int privilege, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_INVITE, listener);
    request->setNodeHandle(chatid);
    request->setParentHandle(uh);
    request->setAccess(privilege);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::removeFromChat(MegaHandle chatid, MegaHandle uh, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_REMOVE, listener);
    request->setNodeHandle(chatid);
    if (uh != INVALID_HANDLE)   // if not provided, it removes oneself from the chat
    {
        request->setParentHandle(uh);
    }
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::getUrlChat(MegaHandle chatid, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_URL, listener);
    request->setNodeHandle(chatid);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::grantAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_GRANT_ACCESS, listener);
    request->setParentHandle(chatid);
    request->setNodeHandle(n->getHandle());

    char uid[12];
    Base64::btoa((byte*)&uh, MegaClient::CHATHANDLE, uid);
    uid[11] = 0;

    request->setEmail(uid);
    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::removeAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_CHAT_REMOVE_ACCESS, listener);
    request->setParentHandle(chatid);
    request->setNodeHandle(n->getHandle());

    char uid[12];
    Base64::btoa((byte*)&uh, MegaClient::CHATHANDLE, uid);
    uid[11] = 0;

    request->setEmail(uid);
    waiter->notify();
}
#endif

MegaUserList* MegaApiImpl::getContacts()
{
    sdkMutex.lock();

	vector<User*> vUsers;
	for (user_map::iterator it = client->users.begin() ; it != client->users.end() ; it++ )
	{
		User *u = &(it->second);
        if (u->userhandle == client->me)
        {
            continue;
        }
        vector<User *>::iterator i = std::lower_bound(vUsers.begin(), vUsers.end(), u, MegaApiImpl::userComparatorDefaultASC);
		vUsers.insert(i, u);
	}
    MegaUserList *userList = new MegaUserListPrivate(vUsers.data(), vUsers.size());

    sdkMutex.unlock();

	return userList;
}


MegaUser* MegaApiImpl::getContact(const char* email)
{
    sdkMutex.lock();
	MegaUser *user = MegaUserPrivate::fromUser(client->finduser(email, 0));

    if (user && user->getHandle() == client->me)
    {
        delete user;
        user = NULL;    // it's not a contact
    }

    sdkMutex.unlock();
	return user;
}


MegaNodeList* MegaApiImpl::getInShares(MegaUser *megaUser)
{
    if (!megaUser)
    {
        return new MegaNodeListPrivate();
    }

    sdkMutex.lock();
    vector<Node*> vNodes;
    User *user = client->finduser(megaUser->getEmail(), 0);
    if (!user || user->show != VISIBLE)
    {
        sdkMutex.unlock();
        return new MegaNodeListPrivate();
    }

    for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
    {
        Node *n;
        if ((n = client->nodebyhandle(*sit)) && !n->parent)
        {
            vNodes.push_back(n);
        }
    }

    MegaNodeList *nodeList;
    if (vNodes.size())
    {
        nodeList = new MegaNodeListPrivate(vNodes.data(), vNodes.size());
    }
    else
    {
        nodeList = new MegaNodeListPrivate();
    }

    sdkMutex.unlock();
    return nodeList;
}

MegaNodeList* MegaApiImpl::getInShares()
{
    sdkMutex.lock();

    vector<Node*> vNodes;
    for (user_map::iterator it = client->users.begin(); it != client->users.end(); it++)
    {
        User *user = &(it->second);
        if (user->show != VISIBLE)
        {
            continue;
        }

        Node *n;
        for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
        {
            if ((n = client->nodebyhandle(*sit)) && !n->parent)
            {
                vNodes.push_back(n);
            }
        }
    }

    MegaNodeList *nodeList = new MegaNodeListPrivate(vNodes.data(), vNodes.size());
    sdkMutex.unlock();
    return nodeList;
}

MegaShareList* MegaApiImpl::getInSharesList()
{
    sdkMutex.lock();

    vector<Share*> vShares;
    handle_vector vHandles;

    for(user_map::iterator it = client->users.begin(); it != client->users.end(); it++)
    {
        User *user = &(it->second);
        if (user->show != VISIBLE)
        {
            continue;
        }

        Node *n;

        for (handle_set::iterator sit = user->sharing.begin(); sit != user->sharing.end(); sit++)
        {
            if ((n = client->nodebyhandle(*sit)) && !n->parent)
            {
                vShares.push_back(n->inshare);
                vHandles.push_back(n->nodehandle);
            }
        }
    }

    MegaShareList *shareList = new MegaShareListPrivate(vShares.data(), vHandles.data(), vShares.size());
    sdkMutex.unlock();
    return shareList;
}

bool MegaApiImpl::isPendingShare(MegaNode *megaNode)
{
    if(!megaNode) return false;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return false;
    }

    bool result = (node->pendingshares != NULL);
    sdkMutex.unlock();

    return result;
}

MegaShareList *MegaApiImpl::getOutShares()
{
    sdkMutex.lock();

    OutShareProcessor shareProcessor;
    processTree(client->nodebyhandle(client->rootnodes[0]), &shareProcessor, true);
    MegaShareList *shareList = new MegaShareListPrivate(shareProcessor.getShares().data(), shareProcessor.getHandles().data(), shareProcessor.getShares().size());

	sdkMutex.unlock();
	return shareList;
}

MegaShareList* MegaApiImpl::getOutShares(MegaNode *megaNode)
{
    if(!megaNode) return new MegaShareListPrivate();

    sdkMutex.lock();
	Node *node = client->nodebyhandle(megaNode->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
        return new MegaShareListPrivate();
	}

    if(!node->outshares)
    {
        sdkMutex.unlock();
        return new MegaShareListPrivate();
    }

	vector<Share*> vShares;
	vector<handle> vHandles;

    for (share_map::iterator it = node->outshares->begin(); it != node->outshares->end(); it++)
	{
        Share *share = it->second;
        if (share->user && (share->user->show == VISIBLE))
        {
            vShares.push_back(share);
            vHandles.push_back(node->nodehandle);
        }
	}

    MegaShareList *shareList = new MegaShareListPrivate(vShares.data(), vHandles.data(), vShares.size());
    sdkMutex.unlock();
    return shareList;
}

MegaShareList *MegaApiImpl::getPendingOutShares()
{
    sdkMutex.lock();

    PendingOutShareProcessor shareProcessor;
    processTree(client->nodebyhandle(client->rootnodes[0]), &shareProcessor, true);
    MegaShareList *shareList = new MegaShareListPrivate(shareProcessor.getShares().data(), shareProcessor.getHandles().data(), shareProcessor.getShares().size());

    sdkMutex.unlock();
    return shareList;
}

MegaShareList *MegaApiImpl::getPendingOutShares(MegaNode *megaNode)
{
    if(!megaNode)
    {
        return new MegaShareListPrivate();
    }

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if(!node || !node->pendingshares)
    {
        sdkMutex.unlock();
        return new MegaShareListPrivate();
    }

    vector<Share*> vShares;
    vector<handle> vHandles;

    for (share_map::iterator it = node->pendingshares->begin(); it != node->pendingshares->end(); it++)
    {
        vShares.push_back(it->second);
        vHandles.push_back(node->nodehandle);
    }

    MegaShareList *shareList = new MegaShareListPrivate(vShares.data(), vHandles.data(), vShares.size());
    sdkMutex.unlock();
    return shareList;
}

MegaNodeList *MegaApiImpl::getPublicLinks()
{
    sdkMutex.lock();

    PublicLinkProcessor linkProcessor;
    processTree(client->nodebyhandle(client->rootnodes[0]), &linkProcessor, true);
    MegaNodeList *nodeList = new MegaNodeListPrivate(linkProcessor.getNodes().data(), linkProcessor.getNodes().size());

    sdkMutex.unlock();
    return nodeList;
}

MegaContactRequestList *MegaApiImpl::getIncomingContactRequests()
{
    sdkMutex.lock();
    vector<PendingContactRequest*> vContactRequests;
    for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
    {
        if(!it->second->isoutgoing && !it->second->removed())
        {
            vContactRequests.push_back(it->second);
        }
    }

    MegaContactRequestList *requestList = new MegaContactRequestListPrivate(vContactRequests.data(), vContactRequests.size());
    sdkMutex.unlock();

    return requestList;
}

MegaContactRequestList *MegaApiImpl::getOutgoingContactRequests()
{
    sdkMutex.lock();
    vector<PendingContactRequest*> vContactRequests;
    for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
    {
        if(it->second->isoutgoing && !it->second->removed())
        {
            vContactRequests.push_back(it->second);
        }
    }

    MegaContactRequestList *requestList = new MegaContactRequestListPrivate(vContactRequests.data(), vContactRequests.size());
    sdkMutex.unlock();

    return requestList;
}

int MegaApiImpl::getAccess(MegaNode* megaNode)
{
    if(!megaNode) return MegaShare::ACCESS_UNKNOWN;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return MegaShare::ACCESS_UNKNOWN;
    }

    if (!client->loggedin())
    {
        sdkMutex.unlock();
        return MegaShare::ACCESS_READ;
    }

    if(node->type > FOLDERNODE)
    {
        sdkMutex.unlock();
        return MegaShare::ACCESS_OWNER;
    }

    Node *n = node;
    accesslevel_t a = OWNER;
    while (n)
    {
        if (n->inshare) { a = n->inshare->access; break; }
        n = n->parent;
    }

    sdkMutex.unlock();

    switch(a)
    {
        case RDONLY: return MegaShare::ACCESS_READ;
        case RDWR: return MegaShare::ACCESS_READWRITE;
        case FULL: return MegaShare::ACCESS_FULL;
        default: return MegaShare::ACCESS_OWNER;
    }
}

bool MegaApiImpl::processMegaTree(MegaNode* n, MegaTreeProcessor* processor, bool recursive)
{
	if(!n) return true;
	if(!processor) return false;

    sdkMutex.lock();
	Node *node = client->nodebyhandle(n->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
		return true;
	}

	if (node->type != FILENODE)
	{
		for (node_list::iterator it = node->children.begin(); it != node->children.end(); )
		{
			MegaNode *megaNode = MegaNodePrivate::fromNode(*it++);
			if(recursive)
			{
				if(!processMegaTree(megaNode,processor))
				{
					delete megaNode;
                    sdkMutex.unlock();
					return 0;
				}
			}
			else
			{
				if(!processor->processMegaNode(megaNode))
				{
					delete megaNode;
                    sdkMutex.unlock();
					return 0;
				}
			}
			delete megaNode;
		}
	}
	bool result = processor->processMegaNode(n);

    sdkMutex.unlock();
    return result;
}

MegaNodeList *MegaApiImpl::search(const char *searchString)
{
    if(!searchString)
    {
        return new MegaNodeListPrivate();
    }

    sdkMutex.lock();

    node_vector result;
    Node *node;

    // rootnodes
    for (unsigned int i = 0; i < (sizeof client->rootnodes / sizeof *client->rootnodes); i++)
    {
        node = client->nodebyhandle(client->rootnodes[i]);

        SearchTreeProcessor searchProcessor(searchString);
        processTree(node, &searchProcessor);
        node_vector& vNodes = searchProcessor.getResults();

        result.insert(result.end(), vNodes.begin(), vNodes.end());
    }

    // inshares
    MegaShareList *shares = getInSharesList();
    for (int i = 0; i < shares->size(); i++)
    {
        node = client->nodebyhandle(shares->get(i)->getNodeHandle());

        SearchTreeProcessor searchProcessor(searchString);
        processTree(node, &searchProcessor);
        vector<Node *>& vNodes  = searchProcessor.getResults();

        result.insert(result.end(), vNodes.begin(), vNodes.end());
    }
    delete shares;

    MegaNodeList *nodeList = new MegaNodeListPrivate(result.data(), result.size());
    
    sdkMutex.unlock();

    return nodeList;
}

MegaNode *MegaApiImpl::createForeignFileNode(MegaHandle handle, const char *key, const char *name, m_off_t size, m_off_t mtime,
                                            MegaHandle parentHandle, const char* privateauth, const char *publicauth)
{
    string nodekey;
    string attrstring;
    nodekey.resize(strlen(key) * 3 / 4 + 3);
    nodekey.resize(Base64::atob(key, (byte *)nodekey.data(), nodekey.size()));
    return new MegaNodePrivate(name, FILENODE, size, mtime, mtime, handle, &nodekey, &attrstring, NULL, parentHandle,
                               privateauth, publicauth, false, true);
}

MegaNode *MegaApiImpl::createForeignFolderNode(MegaHandle handle, const char *name, MegaHandle parentHandle, const char *privateauth, const char *publicauth)
{
    string nodekey;
    string attrstring;
    return new MegaNodePrivate(name, FOLDERNODE, 0, 0, 0, handle, &nodekey, &attrstring, NULL, parentHandle,
                               privateauth, publicauth, false, true);
}

MegaNode *MegaApiImpl::authorizeNode(MegaNode *node)
{
    if (!node || node->getType() != MegaNode::TYPE_FILE)
    {
        return NULL;
    }

    if (node->isPublic() || node->isForeign())
    {
        return node->copy();
    }

    MegaNodePrivate *result = NULL;
    sdkMutex.lock();
    Node *n = client->nodebyhandle(node->getHandle());
    if (n && n->type == FILENODE)
    {
        char *h = NULL;
        result = new MegaNodePrivate(node);
        result->setForeign(true);
        if (client->sid.size())
        {
            h = getAccountAuth();
            result->setPrivateAuth(h);
        }
        else
        {
            h = MegaApi::handleToBase64(client->getrootpublicfolder());
            result->setPublicAuth(h);            
        }
        delete [] h;
    }
    sdkMutex.unlock();
    return result;
}

void MegaApiImpl::loadBalancing(const char* service, MegaRequestListener *listener)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOAD_BALANCING, listener);
    request->setName(service);
    requestQueue.push(request);
    waiter->notify();
}

const char *MegaApiImpl::getVersion()
{
    return client->version();
}

const char *MegaApiImpl::getUserAgent()
{
    return client->useragent.c_str();
}

void MegaApiImpl::changeApiUrl(const char *apiURL, bool disablepkp)
{
    sdkMutex.lock();
    MegaClient::APIURL = apiURL;
    if(disablepkp)
    {
        MegaClient::disablepkp = true;
    }

    client->abortbackoff();
    client->disconnect();
    sdkMutex.unlock();
}

void MegaApiImpl::retrySSLerrors(bool enable)
{
    sdkMutex.lock();
    client->retryessl = enable;
    sdkMutex.unlock();
}

void MegaApiImpl::setPublicKeyPinning(bool enable)
{
    sdkMutex.lock();
    client->disablepkp = !enable;
    sdkMutex.unlock();
}

bool MegaApiImpl::processTree(Node* node, TreeProcessor* processor, bool recursive)
{
	if(!node) return 1;
	if(!processor) return 0;

    sdkMutex.lock();
	node = client->nodebyhandle(node->nodehandle);
	if(!node)
	{
        sdkMutex.unlock();
		return 1;
	}

	if (node->type != FILENODE)
	{
		for (node_list::iterator it = node->children.begin(); it != node->children.end(); )
		{
			if(recursive)
			{
				if(!processTree(*it++,processor))
				{
                    sdkMutex.unlock();
					return 0;
				}
			}
			else
			{
				if(!processor->processNode(*it++))
				{
                    sdkMutex.unlock();
					return 0;
				}
			}
		}
	}
	bool result = processor->processNode(node);

    sdkMutex.unlock();
	return result;
}

MegaNodeList* MegaApiImpl::search(MegaNode* n, const char* searchString, bool recursive)
{
    if (!n || !searchString)
    {
    	return new MegaNodeListPrivate();
    }
    
    sdkMutex.lock();
    
    Node *node = client->nodebyhandle(n->getHandle());
    if (!node)
    {
        sdkMutex.unlock();
        return new MegaNodeListPrivate();
    }

    SearchTreeProcessor searchProcessor(searchString);
    processTree(node, &searchProcessor, recursive);
    vector<Node *>& vNodes = searchProcessor.getResults();

    MegaNodeList *nodeList = new MegaNodeListPrivate(vNodes.data(), vNodes.size());

    sdkMutex.unlock();

    return nodeList;
}

long long MegaApiImpl::getSize(MegaNode *n)
{
    if(!n) return 0;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return 0;
    }
    SizeProcessor sizeProcessor;
    processTree(node, &sizeProcessor);
    long long result = sizeProcessor.getTotalBytes();
    sdkMutex.unlock();

    return result;
}

char *MegaApiImpl::getFingerprint(const char *filePath)
{
    if(!filePath) return NULL;

    string path = filePath;
    string localpath;
    fsAccess->path2local(&path, &localpath);

    FileAccess *fa = fsAccess->newfileaccess();
    if(!fa->fopen(&localpath, true, false))
        return NULL;

    FileFingerprint fp;
    fp.genfingerprint(fa);
    m_off_t size = fa->size;
    delete fa;
    if(fp.size < 0)
        return NULL;

    string fingerprint;
    fp.serializefingerprint(&fingerprint);

    char bsize[sizeof(size)+1];
    int l = Serialize64::serialize((byte *)bsize, size);
    char *buf = new char[l * 4 / 3 + 4];
    char ssize = 'A' + Base64::btoa((const byte *)bsize, l, buf);

    string result(1, ssize);
    result.append(buf);
    result.append(fingerprint);
    delete [] buf;

    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getFingerprint(MegaNode *n)
{
    if(!n) return NULL;

    return MegaApi::strdup(n->getFingerprint());
}

void MegaApiImpl::transfer_failed(Transfer* tr, error e, dstime timeleft)
{
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaError megaError(e, timeleft / 10);
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);
    transfer->setUpdateTime(Waiter::ds);
    transfer->setDeltaSize(0);
    transfer->setSpeed(0);
    transfer->setLastError(megaError);

    if (e == API_EOVERQUOTA && timeleft)
    {
        LOG_warn << "Bandwidth overquota";
        for (int d = GET; d == GET || d == PUT; d += PUT - GET)
        {
            for (transfer_map::iterator it = client->transfers[d].begin(); it != client->transfers[d].end(); it++)
            {
                Transfer *t = it->second;
                t->bt.backoff(timeleft);
                if (t->slot)
                {
                    t->slot->retrybt.backoff(timeleft);
                    t->slot->retrying = true;
                }
            }
        }
    }

    fireOnTransferTemporaryError(transfer, megaError);
}

char *MegaApiImpl::getFingerprint(MegaInputStream *inputStream, int64_t mtime)
{
    if(!inputStream) return NULL;

    ExternalInputStream is(inputStream);
    m_off_t size = is.size();
    if(size < 0)
        return NULL;

    FileFingerprint fp;
    fp.genfingerprint(&is, mtime);

    if(fp.size < 0)
        return NULL;

    string fingerprint;
    fp.serializefingerprint(&fingerprint);

    char bsize[sizeof(size)+1];
    int l = Serialize64::serialize((byte *)bsize, size);
    char *buf = new char[l * 4 / 3 + 4];
    char ssize = 'A' + Base64::btoa((const byte *)bsize, l, buf);

    string result(1, ssize);
    result.append(buf);
    result.append(fingerprint);
    delete [] buf;

    return MegaApi::strdup(result.c_str());
}

MegaNode *MegaApiImpl::getNodeByFingerprint(const char *fingerprint)
{
    if(!fingerprint) return NULL;

    MegaNode *result;
    sdkMutex.lock();
    result = MegaNodePrivate::fromNode(getNodeByFingerprintInternal(fingerprint));
    sdkMutex.unlock();
    return result;
}

MegaNodeList *MegaApiImpl::getNodesByFingerprint(const char *fingerprint)
{
    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return new MegaNodeListPrivate();
    }

    sdkMutex.lock();
    node_vector *nodes = client->nodesbyfingerprint(fp);
    MegaNodeList *result = new MegaNodeListPrivate(nodes->data(), nodes->size());
    delete fp;
    delete nodes;
    sdkMutex.unlock();
    return result;
}

MegaNode *MegaApiImpl::getExportableNodeByFingerprint(const char *fingerprint, const char *name)
{
    MegaNode *result = NULL;

    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return NULL;
    }

    sdkMutex.lock();
    node_vector *nodes = client->nodesbyfingerprint(fp);
    for (unsigned int i = 0; i < nodes->size(); i++)
    {
        Node *node = nodes->at(i);
        if ((!name || !strcmp(name, node->displayname())) &&
                client->checkaccess(node, OWNER))
        {
            Node *n = node;
            while (n)
            {
                if (n->type == RUBBISHNODE)
                {
                    node = NULL;
                    break;
                }
                n = n->parent;
            }

            if (!node)
            {
                continue;
            }

            result = MegaNodePrivate::fromNode(node);
            break;
        }
    }

    delete fp;
    delete nodes;
    sdkMutex.unlock();
    return result;
}

MegaNode *MegaApiImpl::getNodeByFingerprint(const char *fingerprint, MegaNode* parent)
{
    if(!fingerprint) return NULL;

    MegaNode *result;
    sdkMutex.lock();
    Node *p = NULL;
    if(parent)
    {
        p = client->nodebyhandle(parent->getHandle());
    }

    result = MegaNodePrivate::fromNode(getNodeByFingerprintInternal(fingerprint, p));
    sdkMutex.unlock();
    return result;
}

bool MegaApiImpl::hasFingerprint(const char *fingerprint)
{
    return (getNodeByFingerprintInternal(fingerprint) != NULL);
}

char *MegaApiImpl::getCRC(const char *filePath)
{
    if(!filePath) return NULL;

    string path = filePath;
    string localpath;
    fsAccess->path2local(&path, &localpath);

    FileAccess *fa = fsAccess->newfileaccess();
    if(!fa->fopen(&localpath, true, false))
        return NULL;

    FileFingerprint fp;
    fp.genfingerprint(fa);
    delete fa;
    if(fp.size < 0)
        return NULL;

    string result;
    result.resize((sizeof fp.crc) * 4 / 3 + 4);
    result.resize(Base64::btoa((const byte *)fp.crc, sizeof fp.crc, (char*)result.c_str()));
    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getCRCFromFingerprint(const char *fingerprint)
{    
    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return NULL;
    }
    
    string result;
    result.resize((sizeof fp->crc) * 4 / 3 + 4);
    result.resize(Base64::btoa((const byte *)fp->crc, sizeof fp->crc,(char*)result.c_str()));
    delete fp;

    return MegaApi::strdup(result.c_str());
}

char *MegaApiImpl::getCRC(MegaNode *n)
{
    if(!n) return NULL;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node || node->type != FILENODE || node->size < 0 || !node->isvalid)
    {
        sdkMutex.unlock();
        return NULL;
    }

    string result;
    result.resize((sizeof node->crc) * 4 / 3 + 4);
    result.resize(Base64::btoa((const byte *)node->crc, sizeof node->crc, (char*)result.c_str()));

    sdkMutex.unlock();
    return MegaApi::strdup(result.c_str());
}

MegaNode *MegaApiImpl::getNodeByCRC(const char *crc, MegaNode *parent)
{
    if(!parent) return NULL;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(parent->getHandle());
    if(!node || node->type == FILENODE)
    {
        sdkMutex.unlock();
        return NULL;
    }

    byte binarycrc[sizeof(node->crc)];
    Base64::atob(crc, binarycrc, sizeof(binarycrc));

    for (node_list::iterator it = node->children.begin(); it != node->children.end(); it++)
    {
        Node *child = (*it);
        if(!memcmp(child->crc, binarycrc, sizeof(node->crc)))
        {
            MegaNode *result = MegaNodePrivate::fromNode(child);
            sdkMutex.unlock();
            return result;
        }
    }

    sdkMutex.unlock();
    return NULL;
}

SearchTreeProcessor::SearchTreeProcessor(const char *search) { this->search = search; }

#if defined(_WIN32) || defined(__APPLE__)

char *strcasestr(const char *string, const char *substring)
{
	int i, j;
	for (i = 0; string[i]; i++)
	{
		for (j = 0; substring[j]; j++)
		{
			unsigned char c1 = string[i + j];
			if (!c1)
				return NULL;

			unsigned char c2 = substring[j];
			if (toupper(c1) != toupper(c2))
				break;
		}

		if (!substring[j])
			return (char *)string + i;
	}
	return NULL;
}

#endif

bool SearchTreeProcessor::processNode(Node* node)
{
	if(!node) return true;
	if(!search) return false;

	if(strcasestr(node->displayname(), search)!=NULL)
		results.push_back(node);

	return true;
}

vector<Node *> &SearchTreeProcessor::getResults()
{
	return results;
}

SizeProcessor::SizeProcessor()
{
    totalBytes=0;
}

bool SizeProcessor::processNode(Node *node)
{
    if(node->type == FILENODE)
        totalBytes += node->size;
    return true;
}

long long SizeProcessor::getTotalBytes()
{
    return totalBytes;
}

void MegaApiImpl::transfer_added(Transfer *t)
{
	MegaTransferPrivate *transfer = currentTransfer;
    if(!transfer)
    {
        transfer = new MegaTransferPrivate(t->type);
        transfer->setSyncTransfer(true);
    }

	currentTransfer = NULL;
    transfer->setTransfer(t);
    transfer->setTotalBytes(t->size);
    transfer->setTag(t->tag);
	transferMap[t->tag]=transfer;

    if (t->type == GET)
    {
        totalDownloads++;
        pendingDownloads++;
    }
    else
    {
        totalUploads++;
        pendingUploads++;
    }

    fireOnTransferStart(transfer);
}

void MegaApiImpl::transfer_removed(Transfer *t)
{
    if(transferMap.find(t->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(t->tag);
    if(!transfer)
    {
        return;
    }

    if (t->type == GET)
    {
        if(pendingDownloads > 0)
            pendingDownloads--;

        if(totalDownloads > 0)
            totalDownloads--;
    }
    else
    {
        if(pendingUploads > 0)
            pendingUploads--;

        if(totalUploads > 0)
            totalUploads--;
    }

    fireOnTransferFinish(transfer, transfer->getLastError());
}

void MegaApiImpl::transfer_prepare(Transfer *t)
{
    if(transferMap.find(t->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(t->tag);

	if (t->type == GET)
		transfer->setNodeHandle(t->files.back()->h);

    string path;
    fsAccess->local2path(&(t->files.back()->localname), &path);
    transfer->setPath(path.c_str());
    transfer->setTotalBytes(t->size);

    LOG_info << "Transfer (" << transfer->getTransferString() << ") starting. File: " << transfer->getFileName();
}

void MegaApiImpl::transfer_update(Transfer *tr)
{
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);
    if(!transfer)
    {
        return;
    }

    if(tr->slot)
    {
        if((transfer->getUpdateTime() != Waiter::ds) || !tr->slot->progressreported || (tr->slot->progressreported == tr->size))
        {
            if(!transfer->getStartTime())
            {
                transfer->setTotalBytes(tr->size);
                transfer->setStartTime(Waiter::ds);
                transfer->setTransferredBytes(tr->slot->progressreported);
            }

            m_off_t deltaSize = tr->slot->progressreported - transfer->getTransferredBytes();
            transfer->setDeltaSize(deltaSize);

            dstime currentTime = Waiter::ds;
            long long speed = 0;
            if(tr->type == GET)
            {
                totalDownloadedBytes += deltaSize;

                while(downloadBytes.size())
                {
                    dstime deltaTime = currentTime - downloadTimes[0];
                    if(deltaTime <= 50)
                    {
                        break;
                    }

                    downloadPartialBytes -= downloadBytes[0];
                    downloadBytes.erase(downloadBytes.begin());
                    downloadTimes.erase(downloadTimes.begin());
                }

                downloadBytes.push_back(deltaSize);
                downloadTimes.push_back(currentTime);
                downloadPartialBytes += deltaSize;

                downloadSpeed = (downloadPartialBytes * 10) / 50;
                speed = downloadSpeed;
            }
            else
            {
                totalUploadedBytes += deltaSize;

                while(uploadBytes.size())
                {
                    dstime deltaTime = currentTime - uploadTimes[0];
                    if(deltaTime <= 50)
                    {
                        break;
                    }

                    uploadPartialBytes -= uploadBytes[0];
                    uploadBytes.erase(uploadBytes.begin());
                    uploadTimes.erase(uploadTimes.begin());
                }

                uploadBytes.push_back(deltaSize);
                uploadTimes.push_back(currentTime);
                uploadPartialBytes += deltaSize;

                uploadSpeed = (uploadPartialBytes * 10) / 50;
                speed = uploadSpeed;
            }

            transfer->setTransferredBytes(tr->slot->progressreported);

            if(currentTime < transfer->getStartTime())
                transfer->setStartTime(currentTime);

            transfer->setSpeed(speed);
            transfer->setUpdateTime(currentTime);

            fireOnTransferUpdate(transfer);
        }
	}
}

void MegaApiImpl::transfer_complete(Transfer* tr)
{
    if(transferMap.find(tr->tag) == transferMap.end()) return;
    MegaTransferPrivate* transfer = transferMap.at(tr->tag);

    dstime currentTime = Waiter::ds;
    if(!transfer->getStartTime())
        transfer->setStartTime(currentTime);
    if(currentTime<transfer->getStartTime())
        transfer->setStartTime(currentTime);

    transfer->setUpdateTime(currentTime);

    if(tr->size != transfer->getTransferredBytes())
    {
        long long speed = 0;
        long long deltaTime = currentTime-transfer->getStartTime();
        if(deltaTime<=0)
            deltaTime = 1;
        if(transfer->getTotalBytes()>0)
            speed = (10*transfer->getTotalBytes())/deltaTime;

        transfer->setSpeed(speed);
        transfer->setDeltaSize(tr->size - transfer->getTransferredBytes());
        if(tr->type == GET)
            totalDownloadedBytes += transfer->getDeltaSize();
        else
            totalUploadedBytes += transfer->getDeltaSize();

        transfer->setTransferredBytes(tr->size);
    }

    if (tr->type == GET)
    {
        if(pendingDownloads > 0)
            pendingDownloads--;

        string path;
        fsAccess->local2path(&tr->localfilename, &path);
        transfer->setPath(path.c_str());

        fireOnTransferFinish(transfer, MegaError(API_OK));
    }
    else
    {
        if(tr->size != transfer->getTransferredBytes())
        {
            fireOnTransferUpdate(transfer);
        }
        transfer->setTransfer(NULL);
    }
}

void MegaApiImpl::transfer_resume(string *d)
{
    if (!d || d->size() < sizeof(char))
    {
        return;
    }

    MegaFile *file;
    char type = MemAccess::get<char>(d->data());
    switch (type)
    {
    case GET:
        file = MegaFileGet::unserialize(d);
        break;
    case PUT:
        file = MegaFilePut::unserialize(d);
        MegaTransferPrivate* transfer = file->getTransfer();
        Node *parent = client->nodebyhandle(transfer->getParentHandle());
        node_vector *nodes = client->nodesbyfingerprint(file);
        const char *name = transfer->getFileName();
        if (parent && nodes && name)
        {
            for (unsigned int i = 0; i < nodes->size(); i++)
            {
                Node* node = nodes->at(i);
                if (node->parent == parent && !strcmp(node->displayname(), name))
                {
                    // don't resume the upload if the node already exist in the target folder
                    delete file;
                    delete transfer;
                    file = NULL;
                    break;
                }
            }
        }
        delete nodes;
        break;
    }

    if (!file)
    {
        return;
    }

    currentTransfer = file->getTransfer();
    client->nextreqtag();
    client->startxfer((direction_t)type, file);
    waiter->notify();
}

dstime MegaApiImpl::pread_failure(error e, int retry, void* param, dstime timeLeft)
{
    MegaTransferPrivate *transfer = (MegaTransferPrivate *)param;
    transfer->setUpdateTime(Waiter::ds);
    transfer->setDeltaSize(0);
    transfer->setSpeed(0);
    transfer->setLastBytes(NULL);
    if (retry <= transfer->getMaxRetries() && e != API_EINCOMPLETE)
    {	
        fireOnTransferTemporaryError(transfer, MegaError(e, timeLeft / 10));
        LOG_debug << "Streaming temporarily failed " << retry;
        if (retry <= 1)
        {
            return 0;
        }

        return (dstime)(1 << (retry - 1));
    }
    else
    {
        fireOnTransferFinish(transfer, MegaError(e));
        return NEVER;
    }
}

bool MegaApiImpl::pread_data(byte *buffer, m_off_t len, m_off_t, void* param)
{
    MegaTransferPrivate *transfer = (MegaTransferPrivate *)param;

    if(!transfer->getStartTime())
    {
        transfer->setStartTime(Waiter::ds);
    }

    m_off_t deltaSize = len;
    transfer->setDeltaSize(deltaSize);

    dstime currentTime = Waiter::ds;
    long long speed = 0;

    totalDownloadedBytes += deltaSize;
    while(downloadBytes.size())
    {
        dstime deltaTime = currentTime - downloadTimes[0];
        if(deltaTime <= 50)
        {
            break;
        }

        downloadPartialBytes -= downloadBytes[0];
        downloadBytes.erase(downloadBytes.begin());
        downloadTimes.erase(downloadTimes.begin());
    }

    downloadBytes.push_back(deltaSize);
    downloadTimes.push_back(currentTime);
    downloadPartialBytes += deltaSize;

    downloadSpeed = (downloadPartialBytes * 10) / 50;
    speed = downloadSpeed;

    if(currentTime < transfer->getStartTime())
        transfer->setStartTime(currentTime);

    transfer->setSpeed(speed);
    transfer->setUpdateTime(currentTime);
    transfer->setLastBytes((char *)buffer);
    transfer->setDeltaSize(len);
    transfer->setTransferredBytes(transfer->getTransferredBytes()+len);

    bool end = (transfer->getTransferredBytes() == transfer->getTotalBytes());
    fireOnTransferUpdate(transfer);
    if(!fireOnTransferData(transfer) || end)
    {
        fireOnTransferFinish(transfer, end ? MegaError(API_OK) : MegaError(API_EINCOMPLETE));
		return end;
    }
    return true;
}

void MegaApiImpl::reportevent_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_REPORT_EVENT)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::loadbalancing_result(string *servers, error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_LOAD_BALANCING)) return;

    if(!e)
    {
        request->setText(servers->c_str());
    }
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::sessions_killed(handle, error e)
{
    MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_KILL_SESSION)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::cleanrubbishbin_result(error e)
{
    MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CLEAN_RUBBISH_BIN)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::getrecoverylink_result(error e)
{
    MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_RECOVERY_LINK) &&
                    (request->getType() != MegaRequest::TYPE_GET_CANCEL_LINK))) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::queryrecoverylink_result(error e)
{
    MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_QUERY_RECOVERY_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK))) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::queryrecoverylink_result(int type, const char *email, const char *ip, time_t, handle uh, const vector<string> *)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    int reqType = request->getType();
    if(!request || ((reqType != MegaRequest::TYPE_QUERY_RECOVERY_LINK) &&
                    (reqType != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK) &&
                    (reqType != MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK))) return;

    request->setEmail(email);
    request->setFlag(type == RECOVER_WITH_MASTERKEY);
    request->setNumber(type);   // not specified in MegaApi documentation
    request->setText(ip);       // not specified in MegaApi documentation
    request->setNodeHandle(uh); // not specified in MegaApi documentation

    const char *link = request->getLink();
    const char* code;

    byte pwkey[SymmCipher::KEYLENGTH];
    const char *mk64;

    if (reqType == MegaRequest::TYPE_QUERY_RECOVERY_LINK)
    {
        fireOnRequestFinish(request, MegaError());
        return;
    }
    else if (reqType == MegaRequest::TYPE_CONFIRM_RECOVERY_LINK)
    {
        if ((code = strstr(link, "#recover")))
        {
            code += strlen("#recover");
        }
        else
        {
            fireOnRequestFinish(request, MegaError(API_EARGS));
            return;
        }

        switch (type)
        {
        case RECOVER_WITH_MASTERKEY:
            {
                mk64 = request->getPrivateKey();
                if (!mk64)
                {
                    fireOnRequestFinish(request, MegaError(API_EARGS));
                    return;
                }

                int creqtag = client->reqtag;
                client->reqtag = client->restag;
                client->getprivatekey(code);
                client->reqtag = creqtag;
                break;
            }

        case RECOVER_WITHOUT_MASTERKEY:
            {
                client->pw_key(request->getPassword(), pwkey);
                int creqtag = client->reqtag;
                client->reqtag = client->restag;
                client->confirmrecoverylink(code, email, pwkey);
                client->reqtag = creqtag;
                break;
            }

        default:
            LOG_debug << "Unknown type of recovery link";

            fireOnRequestFinish(request, MegaError(API_EARGS));
            return;
        }
    }
    else if (reqType == MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK)
    {
        if (type != CHANGE_EMAIL)
        {
            LOG_debug << "Unknown type of change email link";

            fireOnRequestFinish(request, MegaError(API_EARGS));
            return;
        }

        if ((code = strstr(link, "#verify")))
        {
            code += strlen("#verify");
        }
        else
        {
            fireOnRequestFinish(request, MegaError(API_EARGS));
            return;
        };

        client->pw_key(request->getPassword(), pwkey);

        int creqtag = client->reqtag;
        client->reqtag = client->restag;
        client->validatepwd(pwkey);
        client->reqtag = creqtag;
    }
}

void MegaApiImpl::getprivatekey_result(error e, const byte *privk, const size_t len_privk)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK)) return;

    if (e)
    {
        fireOnRequestFinish(request, MegaError(e));
        return;
    }

    const char *link = request->getLink();
    const char* code;
    if ((code = strstr(link, "#recover")))
    {
        code += strlen("#recover");
    }
    else
    {
        fireOnRequestFinish(request, MegaError(API_EARGS));
        return;
    }

    byte pwkey[SymmCipher::KEYLENGTH];
    client->pw_key(request->getPassword(), pwkey);

    byte mk[SymmCipher::KEYLENGTH];
    Base64::atob(request->getPrivateKey(), mk, sizeof mk);

    // check the private RSA is valid after decryption with master key
    SymmCipher key;
    key.setkey(mk);

    byte privkbuf[AsymmCipher::MAXKEYLENGTH * 2];
    memcpy(privkbuf, privk, len_privk);
    key.ecb_decrypt(privkbuf, len_privk);

    AsymmCipher uk;
    if (!uk.setkey(AsymmCipher::PRIVKEY, privkbuf, len_privk))
    {
        fireOnRequestFinish(request, MegaError(API_EKEY));
    }
    else
    {
        int creqtag = client->reqtag;
        client->reqtag = client->restag;
        client->confirmrecoverylink(code, request->getEmail(), pwkey, mk);
        client->reqtag = creqtag;
    }
}

void MegaApiImpl::confirmrecoverylink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_RECOVERY_LINK)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::confirmcancellink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_CANCEL_LINK)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::validatepassword_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CONFIRM_CANCEL_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK))) return;

    if (e)
    {
        fireOnRequestFinish(request, MegaError(e));
        return;
    }

    if (request->getType() == MegaRequest::TYPE_CONFIRM_CANCEL_LINK)
    {
        const char *link = request->getLink();
        const char* code;
        if ((code = strstr(link, "#cancel")))
        {
            code += strlen("#cancel");
            int creqtag = client->reqtag;
            client->reqtag = client->restag;
            client->confirmcancellink(code);
            client->reqtag = creqtag;
        }
        else
        {
            fireOnRequestFinish(request, MegaError(API_EARGS));
        }
    }
    else if (request->getType() == MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK)
    {
        byte pwkey[SymmCipher::KEYLENGTH];
        client->pw_key(request->getPassword(), pwkey);

        const char* code;
        if ((code = strstr(request->getLink(), "#verify")))
        {
            code += strlen("#verify");
            int creqtag = client->reqtag;
            client->reqtag = client->restag;
            client->confirmemaillink(code, request->getEmail(), pwkey);
            client->reqtag = creqtag;
        }
        else
        {
            fireOnRequestFinish(request, MegaError(API_EARGS));
        }
    }
}

void MegaApiImpl::getemaillink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::confirmemaillink_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK)) return;

    fireOnRequestFinish(request, MegaError(e));
}

#ifdef ENABLE_CHAT

void MegaApiImpl::chatcreate_result(TextChat *chat, error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_CREATE)) return;

    if (!e)
    {
        // encapsulate the chat in a list for the request
        textchat_vector chatList;
        chatList.push_back(chat);

        MegaTextChatListPrivate *megaChatList = new MegaTextChatListPrivate(&chatList);
        request->setMegaTextChatList(megaChatList);
    }

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::chatfetch_result(textchat_vector *chatList, error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_FETCH)) return;

    if (!e)
    {
        MegaTextChatListPrivate *megaChatList = new MegaTextChatListPrivate(chatList);
        request->setMegaTextChatList(megaChatList);
    }

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::chatinvite_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_INVITE)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::chatremove_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_REMOVE)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::chaturl_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_URL)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::chaturl_result(string *url, error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_URL)) return;

    if (!e)
    {
        request->setLink(url->c_str());
    }

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::chatgrantaccess_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_GRANT_ACCESS)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::chatremoveaccess_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CHAT_REMOVE_ACCESS)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::chats_updated(textchat_vector *chats)
{
    if (!chats || !chats->size())
    {
        return;
    }

    MegaTextChatList *chatList = new MegaTextChatListPrivate(chats);
    fireOnChatsUpdate(chatList);
    delete chatList;
}
#endif

#ifdef ENABLE_SYNC
void MegaApiImpl::syncupdate_state(Sync *sync, syncstate_t newstate)
{
    LOG_debug << "Sync state change: " << newstate << " Path: " << sync->localroot.name;
    client->abortbackoff(false);

    if(newstate == SYNC_FAILED)
    {
        MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_ADD_SYNC);

        if(sync->localroot.node)
        {
            request->setNodeHandle(sync->localroot.node->nodehandle);
        }

        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag]=request;
        fireOnRequestFinish(request, MegaError(sync->errorcode));
    }

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);
    megaSync->setState(newstate);

    fireOnSyncStateChanged(megaSync);
}

void MegaApiImpl::syncupdate_scanning(bool scanning)
{
    if(client)
    {
        client->abortbackoff(false);
        client->syncscanstate = scanning;
    }
    fireOnGlobalSyncStateChanged();
}

void MegaApiImpl::syncupdate_local_folder_addition(Sync *sync, LocalNode *, const char* path)
{
    LOG_debug << "Sync - local folder addition detected: " << path;
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FOLDER_ADITION);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_folder_deletion(Sync *sync, LocalNode *localNode)
{
    client->abortbackoff(false);

    string local;
    string path;
    localNode->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);
    LOG_debug << "Sync - local folder deletion detected: " << path.c_str();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);


    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FOLDER_DELETION);
    event->setPath(path.c_str());
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_addition(Sync *sync, LocalNode *, const char* path)
{
    LOG_debug << "Sync - local file addition detected: " << path;
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_ADDITION);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_deletion(Sync *sync, LocalNode *localNode)
{
    client->abortbackoff(false);

    string local;
    string path;
    localNode->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);
    LOG_debug << "Sync - local file deletion detected: " << path.c_str();

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_DELETION);
    event->setPath(path.c_str());
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_file_change(Sync *sync, LocalNode *, const char* path)
{
    LOG_debug << "Sync - local file change detected: " << path;
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_FILE_CHANGED);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_local_move(Sync *sync, LocalNode *localNode, const char *to)
{
    client->abortbackoff(false);

    string local;
    string path;
    localNode->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);
    LOG_debug << "Sync - local rename/move " << path.c_str() << " -> " << to;

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_LOCAL_MOVE);
    event->setPath(path.c_str());
    event->setNewPath(to);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_get(Sync *sync, Node* node, const char *path)
{
    LOG_debug << "Sync - requesting file " << path;

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_FILE_GET);
    event->setNodeHandle(node->nodehandle);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_put(Sync *sync, LocalNode *, const char *path)
{
    LOG_debug << "Sync - sending file " << path;

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_FILE_PUT);
    event->setPath(path);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_file_addition(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote file addition detected " << n->displayname() << " Nhandle: " << LOG_NODEHANDLE(n->nodehandle);
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FILE_ADDITION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_file_deletion(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote file deletion detected " << n->displayname();
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FILE_DELETION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_folder_addition(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote folder addition detected " << n->displayname();
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FOLDER_ADDITION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_folder_deletion(Sync *sync, Node *n)
{
    LOG_debug << "Sync - remote folder deletion detected " << n->displayname();
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_FOLDER_DELETION);
    event->setNodeHandle(n->nodehandle);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_copy(Sync *, const char *name)
{
    LOG_debug << "Sync - creating remote file " << name << " by copying existing remote file";
    client->abortbackoff(false);
}

void MegaApiImpl::syncupdate_remote_move(Sync *sync, Node *n, Node *prevparent)
{
    LOG_debug << "Sync - remote move " << n->displayname() <<
                 " from " << (prevparent ? prevparent->displayname() : "?") <<
                 " to " << (n->parent ? n->parent->displayname() : "?");
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_MOVE);
    event->setNodeHandle(n->nodehandle);
    event->setPrevParent(prevparent ? prevparent->nodehandle : UNDEF);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_remote_rename(Sync *sync, Node *n, const char *prevname)
{
    LOG_debug << "Sync - remote rename from " << prevname << " to " << n->displayname();
    client->abortbackoff(false);

    if(syncMap.find(sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(sync->tag);

    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(MegaSyncEvent::TYPE_REMOTE_RENAME);
    event->setNodeHandle(n->nodehandle);
    event->setPrevName(prevname);
    fireOnSyncEvent(megaSync, event);
}

void MegaApiImpl::syncupdate_treestate(LocalNode *l)
{
    string local;
    string path;
    l->getlocalpath(&local, true);
    fsAccess->local2path(&local, &path);

    if(syncMap.find(l->sync->tag) == syncMap.end()) return;
    MegaSyncPrivate* megaSync = syncMap.at(l->sync->tag);

    fireOnFileSyncStateChanged(megaSync, path.data(), (int)l->ts);
}

bool MegaApiImpl::sync_syncable(Node *node)
{
    if(node->type == FILENODE && !is_syncable(node->size))
    {
        return false;
    }

    const char *name = node->displayname();
    sdkMutex.unlock();
    bool result = is_syncable(name);
    sdkMutex.lock();
    return result;
}

bool MegaApiImpl::sync_syncable(const char *name, string *localpath, string *)
{
    static FileAccess* f = fsAccess->newfileaccess();
    if(f->fopen(localpath) && !is_syncable(f->size))
    {
        return false;
    }

    sdkMutex.unlock();
    bool result =  is_syncable(name);
    sdkMutex.lock();
    return result;
}

void MegaApiImpl::syncupdate_local_lockretry(bool waiting)
{
    if (waiting)
    {
        LOG_debug << "Sync - waiting for local filesystem lock";
    }
    else
    {
        LOG_debug << "Sync - local filesystem lock issue resolved, continuing...";
        client->abortbackoff(false);
    }

    this->waiting = waiting;
    this->fireOnGlobalSyncStateChanged();
}
#endif


// user addition/update (users never get deleted)
void MegaApiImpl::users_updated(User** u, int count)
{
    if(!count)
    {
        return;
    }

    MegaUserList *userList = NULL;
    if(u != NULL)
    {
        userList = new MegaUserListPrivate(u, count);
        fireOnUsersUpdate(userList);
    }
    else
    {
        fireOnUsersUpdate(NULL);
    }
    delete userList;
}

void MegaApiImpl::account_updated()
{
    fireOnAccountUpdate();
}

void MegaApiImpl::pcrs_updated(PendingContactRequest **r, int count)
{
    if(!count)
    {
        return;
    }

    MegaContactRequestList *requestList = NULL;
    if(r != NULL)
    {
        requestList = new MegaContactRequestListPrivate(r, count);
        fireOnContactRequestsUpdate(requestList);
    }
    else
    {
        fireOnContactRequestsUpdate(NULL);
    }
    delete requestList;
}

void MegaApiImpl::setattr_result(handle h, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if (!request || ((request->getType() != MegaRequest::TYPE_RENAME)
            && request->getType() != MegaRequest::TYPE_SET_ATTR_NODE))
    {
        return;
    }

	request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::rename_result(handle h, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_MOVE)) return;

    request->setNodeHandle(h);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::unlink_result(handle h, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_REMOVE) &&
                    (request->getType() != MegaRequest::TYPE_MOVE)))
    {
        return;
    }

    if (request->getType() != MegaRequest::TYPE_MOVE)
    {
        request->setNodeHandle(h);
    }

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::fetchnodes_result(error e)
{    
    MegaError megaError(e);
    MegaRequestPrivate* request;
    if (!client->restag)
    {
        request = new MegaRequestPrivate(MegaRequest::TYPE_FETCH_NODES);
        fireOnRequestFinish(request, megaError);
        return;
    }

    if (requestMap.find(client->restag) == requestMap.end())
    {
        return;
    }
    request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_FETCH_NODES))
    {
        return;
    }

    if (e == API_OK)
    {
        // check if we fetched a folder link and the key is invalid
        handle h = client->getrootpublicfolder();
        if (h != UNDEF)
        {
            Node *n = client->nodebyhandle(h);
            if (n && (n->attrs.map.find('n') == n->attrs.map.end()))
            {
                request->setFlag(true);
            }
        }
    }

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::putnodes_result(error e, targettype_t t, NewNode* nn)
{
    handle h = UNDEF;
    Node *n = NULL;

    if(!e && t != USER_HANDLE)
    {
        if(client->nodenotify.size())
        {
            n = client->nodenotify.back();
        }

        if(n)
        {
            n->applykey();
            n->setattr();
            h = n->nodehandle;
        }
    }

	MegaError megaError(e);
    if(transferMap.find(client->restag) != transferMap.end())
    {
        MegaTransferPrivate* transfer = transferMap.at(client->restag);
        if(transfer->getType() == MegaTransfer::TYPE_DOWNLOAD)
        {
            return;
        }

        if(pendingUploads > 0)
        {
            pendingUploads--;
        }

        transfer->setNodeHandle(h);
        fireOnTransferFinish(transfer, megaError);
        delete [] nn;
        return;
    }

	if(requestMap.find(client->restag) == requestMap.end()) return;
	MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_IMPORT_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CREATE_FOLDER) &&
                    (request->getType() != MegaRequest::TYPE_COPY) &&
                    (request->getType() != MegaRequest::TYPE_MOVE))) return;

    delete [] nn;

    if (request->getType() != MegaRequest::TYPE_MOVE)
    {
        request->setNodeHandle(h);
        fireOnRequestFinish(request, megaError);
    }
    else
    {
        if (!e)
        {
            Node * node = client->nodebyhandle(request->getNodeHandle());
            if (!node)
            {
                e = API_ENOENT;
            }
            else
            {
                request->setNodeHandle(h);
                int creqtag = client->reqtag;
                client->reqtag = request->getTag();
                e = client->unlink(node);
                client->reqtag = creqtag;
            }
        }

        if (e)
        {
            fireOnRequestFinish(request, MegaError(e));
        }
    }
}

void MegaApiImpl::share_result(error e)
{
	MegaError megaError(e);

    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_EXPORT) &&
                    (request->getType() != MegaRequest::TYPE_SHARE))) return;

    //exportnode_result will be called to end the request.
	if(request->getType() == MegaRequest::TYPE_EXPORT)
		return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::share_result(int, error)
{
    //The other callback will be called at the end of the request
}

void MegaApiImpl::setpcr_result(handle h, error e, opcactions_t action)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_INVITE_CONTACT) return;

    if (e)
    {
        LOG_debug << "Outgoing pending contact request failed (" << megaError.getErrorString() << ")";
    }
    else
    {
        switch (action)
        {
            case OPCA_DELETE:
                LOG_debug << "Outgoing pending contact request deleted successfully";
                break;
            case OPCA_REMIND:
                LOG_debug << "Outgoing pending contact request reminded successfully";
                break;
            case OPCA_ADD:
                char buffer[12];
                Base64::btoa((byte*)&h, sizeof(h), buffer);
                LOG_debug << "Outgoing pending contact request succeeded, id: " << buffer;
                break;
        }
    }

    request->setNodeHandle(h);
    request->setNumber(action);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::updatepcr_result(error e, ipcactions_t action)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_REPLY_CONTACT_REQUEST) return;

    if (e)
    {
        LOG_debug << "Incoming pending contact request update failed (" << megaError.getErrorString() << ")";
    }
    else
    {
        string labels[3] = {"accepted", "denied", "ignored"};
        LOG_debug << "Incoming pending contact request successfully " << labels[(int)action];
    }

    request->setNumber(action);
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::fa_complete(Node*, fatype, const char* data, uint32_t len)
{
    int tag = client->restag;
    while(tag)
    {
        if(requestMap.find(tag) == requestMap.end()) return;
        MegaRequestPrivate* request = requestMap.at(tag);
        if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_FILE)) return;

        tag = request->getNumber();

        FileAccess *f = client->fsaccess->newfileaccess();
        string filePath(request->getFile());
        string localPath;
        fsAccess->path2local(&filePath, &localPath);

        totalDownloadedBytes += len;

        fsAccess->unlinklocal(&localPath);
        if(!f->fopen(&localPath, false, true))
        {
            delete f;
            fireOnRequestFinish(request, MegaError(API_EWRITE));
            continue;
        }

        if(!f->fwrite((const byte*)data, len, 0))
        {
            delete f;
            fireOnRequestFinish(request, MegaError(API_EWRITE));
            continue;
        }

        delete f;
        fireOnRequestFinish(request, MegaError(API_OK));
    }
}

int MegaApiImpl::fa_failed(handle, fatype, int retries, error e)
{
    int tag = client->restag;
    while(tag)
    {
        if(requestMap.find(tag) == requestMap.end()) return 1;
        MegaRequestPrivate* request = requestMap.at(tag);
        if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_FILE))
            return 1;

        tag = request->getNumber();
        if(retries >= 2)
        {
            fireOnRequestFinish(request, MegaError(e));
        }
        else
        {
            fireOnRequestTemporaryError(request, MegaError(e));
        }
    }

    return (retries >= 2);
}

void MegaApiImpl::putfa_result(handle, fatype, error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_SET_ATTR_FILE)
        return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::putfa_result(handle, fatype, const char *)
{
    MegaError megaError(API_OK);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_SET_ATTR_FILE)
        return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::enumeratequotaitems_result(handle product, unsigned prolevel, unsigned gbstorage, unsigned gbtransfer, unsigned months, unsigned amount, const char* currency, const char* description, const char* iosid, const char* androidid)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PRICING) &&
                    (request->getType() != MegaRequest::TYPE_GET_PAYMENT_ID) &&
                    (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT)))
    {
        return;
    }

    request->addProduct(product, prolevel, gbstorage, gbtransfer, months, amount, currency, description, iosid, androidid);
}

void MegaApiImpl::enumeratequotaitems_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PRICING) &&
                    (request->getType() != MegaRequest::TYPE_GET_PAYMENT_ID) &&
                    (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT)))
    {
        return;
    }

    if(request->getType() == MegaRequest::TYPE_GET_PRICING)
    {
        fireOnRequestFinish(request, MegaError(e));
    }
    else
    {
        MegaPricing *pricing = request->getPricing();
        int i;
        for(i = 0; i < pricing->getNumProducts(); i++)
        {
            if(pricing->getHandle(i) == request->getNodeHandle())
            {
                requestMap.erase(request->getTag());
                int nextTag = client->nextreqtag();
                request->setTag(nextTag);
                requestMap[nextTag]=request;
                client->purchase_additem(0, request->getNodeHandle(), pricing->getAmount(i),
                                         pricing->getCurrency(i), 0, NULL, NULL);
                break;
            }
        }

        if(i == pricing->getNumProducts())
        {
            fireOnRequestFinish(request, MegaError(API_ENOENT));
        }
        delete pricing;
    }
}

void MegaApiImpl::additem_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_GET_PAYMENT_ID) &&
                    (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT))) return;

    if(e != API_OK)
    {
        client->purchase_begin();
        fireOnRequestFinish(request, MegaError(e));
        return;
    }

    if(request->getType() == MegaRequest::TYPE_GET_PAYMENT_ID)
    {
        char saleid[16];
        Base64::btoa((byte *)&client->purchase_basket.back(), 8, saleid);
        request->setLink(saleid);
        client->purchase_begin();
        fireOnRequestFinish(request, MegaError(API_OK));
        return;
    }

    //MegaRequest::TYPE_UPGRADE_ACCOUNT
    int method = request->getNumber();
    client->purchase_checkout(method);
}

void MegaApiImpl::checkout_result(const char *errortype, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_UPGRADE_ACCOUNT)) return;

    if(!errortype)
    {
        fireOnRequestFinish(request, MegaError(e));
        return;
    }

    if(!strcmp(errortype, "FP"))
    {
        fireOnRequestFinish(request, MegaError(e - 100));
        return;
    }

    fireOnRequestFinish(request, MegaError(MegaError::PAYMENT_EGENERIC));
    return;
}

void MegaApiImpl::submitpurchasereceipt_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::creditcardquerysubscriptions_result(int number, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS)) return;

    request->setNumber(number);
    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::creditcardcancelsubscriptions_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS)) return;

    fireOnRequestFinish(request, MegaError(e));
}
void MegaApiImpl::getpaymentmethods_result(int methods, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_PAYMENT_METHODS)) return;

    request->setNumber(methods);
    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::userfeedbackstore_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SUBMIT_FEEDBACK)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::sendevent_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SEND_EVENT)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::creditcardstore_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_CREDIT_CARD_STORE)) return;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::copysession_result(string *session, error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_SESSION_TRANSFER_URL)) return;

    const char *path = request->getText();
    string *data = NULL;
    if(e == API_OK)
    {
        data = client->sessiontransferdata(path, session);
    }

    if(data)
    {
        data->insert(0, "https://mega.nz/#sitetransfer!");
    }
    else
    {
        data = new string("https://mega.nz/#");
        if(path)
        {
            data->append(path);
        }
    }

    request->setLink(data->c_str());
    delete data;

    fireOnRequestFinish(request, MegaError(e));
}

void MegaApiImpl::clearing()
{

}

void MegaApiImpl::notify_retry(dstime dsdelta)
{
#ifdef ENABLE_SYNC
    bool previousFlag = waitingRequest;
#endif

    if(!dsdelta)
        waitingRequest = false;
    else if(dsdelta > 10)
        waitingRequest = true;

#ifdef ENABLE_SYNC
    if(previousFlag != waitingRequest)
        fireOnGlobalSyncStateChanged();
#endif

    if (dsdelta && requestMap.size() == 1)
    {
        MegaRequestPrivate *request = requestMap.begin()->second;
        fireOnRequestTemporaryError(request, MegaError(API_EAGAIN));
    }
}

// callback for non-EAGAIN request-level errors
// retrying is futile
// this can occur e.g. with syntactically malformed requests (due to a bug) or due to an invalid application key
void MegaApiImpl::request_error(error e)
{
    MegaRequestPrivate *request = new MegaRequestPrivate(MegaRequest::TYPE_LOGOUT);
    request->setFlag(false);
    request->setParamType(e);

    if (e == API_ESSL && client->sslfakeissuer.size())
    {
        request->setText(client->sslfakeissuer.c_str());
    }

    if (e == API_ESID)
    {
        client->removecaches();
        client->locallogout();
    }

    requestQueue.push(request);
    waiter->notify();
}

void MegaApiImpl::request_response_progress(m_off_t currentProgress, m_off_t totalProgress)
{
    if(requestMap.size() == 1)
    {
        MegaRequestPrivate *request = requestMap.begin()->second;
        if(request && request->getType() == MegaRequest::TYPE_FETCH_NODES)
        {
            if(request->getTransferredBytes() != currentProgress)
            {
                request->setTransferredBytes(currentProgress);
                if(totalProgress != -1)
                {
                    request->setTotalBytes(totalProgress);
                }
                fireOnRequestUpdate(request);
            }
        }
    }
}

// login result
void MegaApiImpl::login_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_LOGIN)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::logout_result(error e)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_LOGOUT)) return;

    if(!e || e == API_ESID)
    {
        requestMap.erase(request->getTag());

        error preverror = (error)request->getParamType();
        while(!requestMap.empty())
        {
            std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
            if(it->second) fireOnRequestFinish(it->second, MegaError(preverror ? preverror : API_EACCESS));
        }

        while(!transferMap.empty())
        {
            std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
            if(it->second) fireOnTransferFinish(it->second, MegaError(preverror ? preverror : API_EACCESS));
        }

        pendingUploads = 0;
        pendingDownloads = 0;
        totalUploads = 0;
        totalDownloads = 0;
        waiting = false;
        waitingRequest = false;
        excludedNames.clear();
        syncLowerSizeLimit = 0;
        syncUpperSizeLimit = 0;
        uploadSpeed = 0;
        downloadSpeed = 0;
        downloadTimes.clear();
        downloadBytes.clear();
        uploadTimes.clear();
        uploadBytes.clear();
        uploadPartialBytes = 0;
        downloadPartialBytes = 0;

        fireOnRequestFinish(request, MegaError(preverror));
        return;
    }
    fireOnRequestFinish(request,MegaError(e));
}

void MegaApiImpl::userdata_result(string *name, string* pubk, string* privk, handle bjid, error result)
{
    MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_USER_DATA)) return;

    if(result == API_OK)
    {
        char jid[16];
        Base32::btoa((byte *)&bjid, MegaClient::USERHANDLE, jid);

        request->setPassword(pubk->c_str());
        request->setPrivateKey(privk->c_str());
        request->setName(name->c_str());
        request->setText(jid);
    }
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::pubkey_result(User *u)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_USER_DATA)) return;

    if(!u)
    {
        fireOnRequestFinish(request, MegaError(API_ENOENT));
        return;
    }

    if(!u->pubk.isvalid())
    {
        fireOnRequestFinish(request, MegaError(API_EACCESS));
        return;
    }

    string key;
    u->pubk.serializekey(&key, AsymmCipher::PUBKEY);
    char pubkbuf[AsymmCipher::MAXKEYLENGTH * 4 / 3 + 4];
    Base64::btoa((byte *)key.data(), key.size(), pubkbuf);
    request->setPassword(pubkbuf);

    char jid[16];
    Base32::btoa((byte *)&u->userhandle, MegaClient::USERHANDLE, jid);
    request->setText(jid);

    if(u->email.size())
    {
        request->setEmail(u->email.c_str());
    }

    fireOnRequestFinish(request, MegaError(API_OK));
}

// password change result
void MegaApiImpl::changepw_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_CHANGE_PW) return;

    fireOnRequestFinish(request, megaError);
}

// node export failed
void MegaApiImpl::exportnode_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_EXPORT) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::exportnode_result(handle h, handle ph)
{
    Node* n;
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || request->getType() != MegaRequest::TYPE_EXPORT) return;

    if ((n = client->nodebyhandle(h)))
    {
        char node[9];
        char key[FILENODEKEYLENGTH*4/3+3];

        Base64::btoa((byte*)&ph,MegaClient::NODEHANDLE,node);

        // the key
        if (n->type == FILENODE)
        {
            if(n->nodekey.size()>=FILENODEKEYLENGTH)
                Base64::btoa((const byte*)n->nodekey.data(),FILENODEKEYLENGTH,key);
            else
                key[0]=0;
        }
        else if (n->sharekey) Base64::btoa(n->sharekey->key,FOLDERNODEKEYLENGTH,key);
        else
        {
            fireOnRequestFinish(request, MegaError(MegaError::API_EKEY));
            return;
        }

        string link = "https://mega.nz/#";
        link += (n->type ? "F" : "");
        link += "!";
        link += node;
        link += "!";
        link += key;
        request->setLink(link.c_str());
        fireOnRequestFinish(request, MegaError(MegaError::API_OK));
    }
    else
    {
        request->setNodeHandle(UNDEF);
        fireOnRequestFinish(request, MegaError(MegaError::API_ENOENT));
    }
}

// the requested link could not be opened
void MegaApiImpl::openfilelink_result(error result)
{
	MegaError megaError(result);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_IMPORT_LINK) &&
                    (request->getType() != MegaRequest::TYPE_GET_PUBLIC_NODE))) return;

    fireOnRequestFinish(request, megaError);
}

// the requested link was opened successfully
// (it is the application's responsibility to delete n!)
void MegaApiImpl::openfilelink_result(handle ph, const byte* key, m_off_t size, string* a, string*, int)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_IMPORT_LINK) &&
                    (request->getType() != MegaRequest::TYPE_GET_PUBLIC_NODE))) return;

	if (!client->loggedin() && (request->getType() == MegaRequest::TYPE_IMPORT_LINK))
	{
        fireOnRequestFinish(request, MegaError(MegaError::API_EACCESS));
		return;
	}

    // no key provided --> check only that the nodehandle is valid
    if (!key && (request->getType() == MegaRequest::TYPE_GET_PUBLIC_NODE))
    {
        fireOnRequestFinish(request, MegaError(MegaError::API_EINCOMPLETE));
        return;
    }

    string attrstring;
    string fileName;
    string keystring;
    string fingerprint;

    attrstring.resize(a->length()*4/3+4);
    attrstring.resize(Base64::btoa((const byte *)a->data(),a->length(), (char *)attrstring.data()));

    m_time_t mtime = 0;

    SymmCipher nodeKey;
    keystring.assign((char*)key,FILENODEKEYLENGTH);
    nodeKey.setkey(key, FILENODE);

    byte *buf = Node::decryptattr(&nodeKey,attrstring.c_str(),attrstring.size());
    if(buf)
    {
        JSON json;
        nameid name;
        string* t;
        AttrMap attrs;

        json.begin((char*)buf+5);
        while ((name = json.getnameid()) != EOO && json.storeobject((t = &attrs.map[name])))
            JSON::unescape(t);

        delete[] buf;

        attr_map::iterator it;
        it = attrs.map.find('n');
        if (it == attrs.map.end()) fileName = "CRYPTO_ERROR";
        else if (!it->second.size()) fileName = "BLANK";
        else fileName = it->second.c_str();

        it = attrs.map.find('c');
        if(it != attrs.map.end())
        {
            FileFingerprint ffp;
            if(ffp.unserializefingerprint(&it->second))
            {
                mtime = ffp.mtime;

                char bsize[sizeof(size)+1];
                int l = Serialize64::serialize((byte *)bsize, size);
                char *buf = new char[l * 4 / 3 + 4];
                char ssize = 'A' + Base64::btoa((const byte *)bsize, l, buf);

                string result(1, ssize);
                result.append(buf);
                result.append(it->second);
                delete [] buf;

                fingerprint = result;
            }
        }
    }
    else
    {
        fileName = "CRYPTO_ERROR";
        request->setFlag(true);
    }

	if(request->getType() == MegaRequest::TYPE_IMPORT_LINK)
	{
		NewNode* newnode = new NewNode[1];

		// set up new node as folder node
		newnode->source = NEW_PUBLIC;
		newnode->type = FILENODE;
		newnode->nodehandle = ph;
        newnode->parenthandle = UNDEF;
		newnode->nodekey.assign((char*)key,FILENODEKEYLENGTH);
        newnode->attrstring = new string(*a);

		// add node
        requestMap.erase(request->getTag());
        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag]=request;
        client->putnodes(request->getParentHandle(), newnode, 1);
	}
	else
	{
        request->setPublicNode(new MegaNodePrivate(fileName.c_str(), FILENODE, size, 0, mtime, ph, &keystring, a,
                                                   fingerprint.size() ? fingerprint.c_str() : NULL,
                                                   INVALID_HANDLE));
        fireOnRequestFinish(request, MegaError(MegaError::API_OK));
	}
}

// reload needed
void MegaApiImpl::reload(const char*)
{
    fireOnReloadNeeded();
}

// nodes have been modified
// (nodes with their removed flag set will be deleted immediately after returning from this call,
// at which point their pointers will become invalid at that point.)
void MegaApiImpl::nodes_updated(Node** n, int count)
{
    if(!count)
    {
        return;
    }

    MegaNodeList *nodeList = NULL;
    if(n != NULL)
    {
        nodeList = new MegaNodeListPrivate(n, count);
        fireOnNodesUpdate(nodeList);
    }
    else
    {
        fireOnNodesUpdate(NULL);
    }
    delete nodeList;
}

void MegaApiImpl::account_details(AccountDetails*, bool, bool, bool, bool, bool, bool)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_ACCOUNT_DETAILS)) return;

	int numDetails = request->getNumDetails();
	numDetails--;
	request->setNumDetails(numDetails);
	if(!numDetails)
    {
        if(!request->getAccountDetails()->storage_max)
            fireOnRequestFinish(request, MegaError(MegaError::API_EACCESS));
        else
            fireOnRequestFinish(request, MegaError(MegaError::API_OK));
    }
}

void MegaApiImpl::account_details(AccountDetails*, error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_ACCOUNT_DETAILS)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::removecontact_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_REMOVE_CONTACT)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::putua_result(error e)
{
    MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_SET_ATTR_USER)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::getua_result(error e)
{
	MegaError megaError(e);
	if(requestMap.find(client->restag) == requestMap.end()) return;
	MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_USER)) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::getua_result(byte* data, unsigned len)
{
	if(requestMap.find(client->restag) == requestMap.end()) return;
	MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || (request->getType() != MegaRequest::TYPE_GET_ATTR_USER)) return;

    if(request->getParamType() == MegaApi::USER_ATTR_AVATAR)
    {
        if (len)
        {
            FileAccess *f = client->fsaccess->newfileaccess();
            string filePath(request->getFile());
            string localPath;
            fsAccess->path2local(&filePath, &localPath);

            totalDownloadedBytes += len;

            fsAccess->unlinklocal(&localPath);
            if(!f->fopen(&localPath, false, true))
            {
                delete f;
                fireOnRequestFinish(request, MegaError(API_EWRITE));
                return;
            }

            if(!f->fwrite((const byte*)data, len, 0))
            {
                delete f;
                fireOnRequestFinish(request, MegaError(API_EWRITE));
                return;
            }

            delete f;
        }
        else    // no data for the avatar
        {
            fireOnRequestFinish(request, MegaError(API_ENOENT));
            return;
        }
    }
    else
    {
        string str((const char*)data,len);
        request->setText(str.c_str());
    }
    fireOnRequestFinish(request, MegaError(API_OK));
}

// user attribute update notification
void MegaApiImpl::userattr_update(User*, int, const char*)
{
}

void MegaApiImpl::ephemeral_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::ephemeral_result(handle, const byte*)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    requestMap.erase(request->getTag());
    int nextTag = client->nextreqtag();
    request->setTag(nextTag);
    requestMap[nextTag] = request;

	byte pwkey[SymmCipher::KEYLENGTH];
    if(!request->getPrivateKey())
		client->pw_key(request->getPassword(),pwkey);
	else
		Base64::atob(request->getPrivateKey(), (byte *)pwkey, sizeof pwkey);

    client->sendsignuplink(request->getEmail(),request->getName(),pwkey);

    int creqtag = client->reqtag;
    client->reqtag = 0;

    if (request->getName())
    {
        client->putua("firstname", (const byte*) request->getName(), strlen(request->getName()));
    }
    if (request->getText())
    {
        client->putua("lastname", (const byte*) request->getText(), strlen(request->getText()));
    }

    client->reqtag = creqtag;
}

void MegaApiImpl::sendsignuplink_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_CREATE_ACCOUNT))) return;

    requestMap.erase(request->getTag());
    while(!requestMap.empty())
    {
        std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
        if(it->second) fireOnRequestFinish(it->second, MegaError(MegaError::API_EACCESS));
    }

    while(!transferMap.empty())
    {
        std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
        if(it->second) fireOnTransferFinish(it->second, MegaError(MegaError::API_EACCESS));
    }

    client->locallogout();
    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::querysignuplink_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_QUERY_SIGNUP_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_ACCOUNT))) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::querysignuplink_result(handle, const char* email, const char* name, const byte* pwc, const byte*, const byte* c, size_t len)
{
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request || ((request->getType() != MegaRequest::TYPE_QUERY_SIGNUP_LINK) &&
                    (request->getType() != MegaRequest::TYPE_CONFIRM_ACCOUNT))) return;

	request->setEmail(email);
	request->setName(name);

	if(request->getType() == MegaRequest::TYPE_QUERY_SIGNUP_LINK)
	{
        fireOnRequestFinish(request, MegaError(API_OK));
		return;
	}

	string signupemail = email;
	string signupcode;
	signupcode.assign((char*)c,len);

	byte signuppwchallenge[SymmCipher::KEYLENGTH];
	byte signupencryptedmasterkey[SymmCipher::KEYLENGTH];

	memcpy(signuppwchallenge,pwc,sizeof signuppwchallenge);
	memcpy(signupencryptedmasterkey,pwc,sizeof signupencryptedmasterkey);

	byte pwkey[SymmCipher::KEYLENGTH];
    if(!request->getPrivateKey())
		client->pw_key(request->getPassword(),pwkey);
	else
		Base64::atob(request->getPrivateKey(), (byte *)pwkey, sizeof pwkey);

	// verify correctness of supplied signup password
	SymmCipher pwcipher(pwkey);
	pwcipher.ecb_decrypt(signuppwchallenge);

	if (*(uint64_t*)(signuppwchallenge+4))
	{
        fireOnRequestFinish(request, MegaError(API_ENOENT));
	}
	else
	{
		// decrypt and set master key, then proceed with the confirmation
		pwcipher.ecb_decrypt(signupencryptedmasterkey);
		client->key.setkey(signupencryptedmasterkey);

        requestMap.erase(request->getTag());
        int nextTag = client->nextreqtag();
        request->setTag(nextTag);
        requestMap[nextTag] = request;

		client->confirmsignuplink((const byte*)signupcode.data(),signupcode.size(),MegaClient::stringhash64(&signupemail,&pwcipher));
	}
}

void MegaApiImpl::confirmsignuplink_result(error e)
{
	MegaError megaError(e);
    if(requestMap.find(client->restag) == requestMap.end()) return;
    MegaRequestPrivate* request = requestMap.at(client->restag);
    if(!request) return;

    fireOnRequestFinish(request, megaError);
}

void MegaApiImpl::setkeypair_result(error)
{

}

void MegaApiImpl::checkfile_result(handle h, error e)
{
    if(e)
    {
        for(std::map<int, MegaTransferPrivate *>::iterator iter = transferMap.begin(); iter != transferMap.end(); iter++)
        {
            MegaTransferPrivate *transfer = iter->second;
            if(transfer->getNodeHandle() == h)
                fireOnTransferTemporaryError(transfer, MegaError(e));
        }
    }
}

void MegaApiImpl::checkfile_result(handle h, error e, byte*, m_off_t, m_time_t, m_time_t, string*, string*, string*)
{
    if(e)
    {
        for(std::map<int, MegaTransferPrivate *>::iterator iter = transferMap.begin(); iter != transferMap.end(); iter++)
        {
            MegaTransferPrivate *transfer = iter->second;
            if(transfer->getNodeHandle() == h)
                fireOnTransferTemporaryError(transfer, MegaError(e));
        }
    }
}

void MegaApiImpl::addListener(MegaListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    listeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::addRequestListener(MegaRequestListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    requestListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::addTransferListener(MegaTransferListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    transferListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::addGlobalListener(MegaGlobalListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    globalListeners.insert(listener);
    sdkMutex.unlock();
}

#ifdef ENABLE_SYNC
void MegaApiImpl::addSyncListener(MegaSyncListener *listener)
{
    if(!listener) return;

    sdkMutex.lock();
    syncListeners.insert(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeSyncListener(MegaSyncListener *listener)
{
    if(!listener) return;

    sdkMutex.lock();
    syncListeners.erase(listener);

    std::map<int, MegaSyncPrivate*>::iterator it = syncMap.begin();
    while(it != syncMap.end())
    {
        MegaSyncPrivate* sync = it->second;
        if(sync->getListener() == listener)
            sync->setListener(NULL);

        it++;
    }
    requestQueue.removeListener(listener);

    sdkMutex.unlock();
}
#endif

void MegaApiImpl::removeListener(MegaListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    listeners.erase(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeRequestListener(MegaRequestListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    requestListeners.erase(listener);

    std::map<int, MegaRequestPrivate*>::iterator it = requestMap.begin();
    while(it != requestMap.end())
    {
        MegaRequestPrivate* request = it->second;
        if(request->getListener() == listener)
            request->setListener(NULL);

        it++;
    }

    requestQueue.removeListener(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeTransferListener(MegaTransferListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    transferListeners.erase(listener);

    std::map<int, MegaTransferPrivate*>::iterator it = transferMap.begin();
    while(it != transferMap.end())
    {
        MegaTransferPrivate* transfer = it->second;
        if(transfer->getListener() == listener)
            transfer->setListener(NULL);

        it++;
    }

    transferQueue.removeListener(listener);
    sdkMutex.unlock();
}

void MegaApiImpl::removeGlobalListener(MegaGlobalListener* listener)
{
    if(!listener) return;

    sdkMutex.lock();
    globalListeners.erase(listener);
    sdkMutex.unlock();
}

MegaRequest *MegaApiImpl::getCurrentRequest()
{
    return activeRequest;
}

MegaTransfer *MegaApiImpl::getCurrentTransfer()
{
    return activeTransfer;
}

MegaError *MegaApiImpl::getCurrentError()
{
    return activeError;
}

MegaNodeList *MegaApiImpl::getCurrentNodes()
{
    return activeNodes;
}

MegaUserList *MegaApiImpl::getCurrentUsers()
{
    return activeUsers;
}

void MegaApiImpl::fireOnRequestStart(MegaRequestPrivate *request)
{
    activeRequest = request;
    LOG_info << "Request (" << request->getRequestString() << ") starting";
	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestStart(api, request);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestStart(api, request);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestStart(api, request);
	activeRequest = NULL;
}


void MegaApiImpl::fireOnRequestFinish(MegaRequestPrivate *request, MegaError e)
{
	MegaError *megaError = new MegaError(e);
	activeRequest = request;
	activeError = megaError;

    if(e.getErrorCode())
    {
        LOG_warn << "Request (" << request->getRequestString() << ") finished with error: " << e.getErrorString();
    }
    else
    {
        LOG_info << "Request (" << request->getRequestString() << ") finished";
    }

	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestFinish(api, request, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestFinish(api, request, megaError);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestFinish(api, request, megaError);

    requestMap.erase(request->getTag());

	activeRequest = NULL;
	activeError = NULL;
	delete request;
    delete megaError;
}

void MegaApiImpl::fireOnRequestUpdate(MegaRequestPrivate *request)
{
    activeRequest = request;

    for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
        (*it)->onRequestUpdate(api, request);

    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onRequestUpdate(api, request);

    MegaRequestListener* listener = request->getListener();
    if(listener) listener->onRequestUpdate(api, request);

    activeRequest = NULL;
}

void MegaApiImpl::fireOnRequestTemporaryError(MegaRequestPrivate *request, MegaError e)
{
	MegaError *megaError = new MegaError(e);
	activeRequest = request;
	activeError = megaError;

    request->setNumRetry(request->getNumRetry() + 1);

	for(set<MegaRequestListener *>::iterator it = requestListeners.begin(); it != requestListeners.end() ; it++)
		(*it)->onRequestTemporaryError(api, request, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onRequestTemporaryError(api, request, megaError);

	MegaRequestListener* listener = request->getListener();
	if(listener) listener->onRequestTemporaryError(api, request, megaError);

	activeRequest = NULL;
	activeError = NULL;
	delete megaError;
}

void MegaApiImpl::fireOnTransferStart(MegaTransferPrivate *transfer)
{
	activeTransfer = transfer;

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferStart(api, transfer);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferStart(api, transfer);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferStart(api, transfer);

	activeTransfer = NULL;
}

void MegaApiImpl::fireOnTransferFinish(MegaTransferPrivate *transfer, MegaError e)
{
	MegaError *megaError = new MegaError(e);
	activeTransfer = transfer;
	activeError = megaError;

    if(e.getErrorCode())
    {
        LOG_warn << "Transfer (" << transfer->getTransferString() << ") finished with error: " << e.getErrorString()
                    << " File: " << transfer->getFileName();
    }
    else
    {
        LOG_info << "Transfer (" << transfer->getTransferString() << ") finished. File: " << transfer->getFileName();
    }

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferFinish(api, transfer, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferFinish(api, transfer, megaError);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferFinish(api, transfer, megaError);

    transferMap.erase(transfer->getTag());

	activeTransfer = NULL;
	activeError = NULL;
	delete transfer;
	delete megaError;
}

void MegaApiImpl::fireOnTransferTemporaryError(MegaTransferPrivate *transfer, MegaError e)
{
	MegaError *megaError = new MegaError(e);
	activeTransfer = transfer;
	activeError = megaError;

    transfer->setNumRetry(transfer->getNumRetry() + 1);

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferTemporaryError(api, transfer, megaError);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferTemporaryError(api, transfer, megaError);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferTemporaryError(api, transfer, megaError);

	activeTransfer = NULL;
	activeError = NULL;
    delete megaError;
}

MegaClient *MegaApiImpl::getMegaClient()
{
    return client;
}

void MegaApiImpl::fireOnTransferUpdate(MegaTransferPrivate *transfer)
{
	activeTransfer = transfer;

	for(set<MegaTransferListener *>::iterator it = transferListeners.begin(); it != transferListeners.end() ; it++)
		(*it)->onTransferUpdate(api, transfer);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onTransferUpdate(api, transfer);

	MegaTransferListener* listener = transfer->getListener();
	if(listener) listener->onTransferUpdate(api, transfer);

	activeTransfer = NULL;
}

bool MegaApiImpl::fireOnTransferData(MegaTransferPrivate *transfer)
{
	activeTransfer = transfer;
	bool result = false;
	MegaTransferListener* listener = transfer->getListener();
	if(listener)
    {
		result = listener->onTransferData(api, transfer, transfer->getLastBytes(), transfer->getDeltaSize());
    }

	activeTransfer = NULL;
	return result;
}

void MegaApiImpl::fireOnUsersUpdate(MegaUserList *users)
{
	activeUsers = users;

	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onUsersUpdate(api, users);
    }
	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onUsersUpdate(api, users);
    }

    activeUsers = NULL;
}

void MegaApiImpl::fireOnContactRequestsUpdate(MegaContactRequestList *requests)
{
    activeContactRequests = requests;

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onContactRequestsUpdate(api, requests);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onContactRequestsUpdate(api, requests);
    }

    activeContactRequests = NULL;
}

void MegaApiImpl::fireOnNodesUpdate(MegaNodeList *nodes)
{
	activeNodes = nodes;

	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onNodesUpdate(api, nodes);
    }
	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onNodesUpdate(api, nodes);
    }

    activeNodes = NULL;
}

void MegaApiImpl::fireOnAccountUpdate()
{
    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onAccountUpdate(api);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onAccountUpdate(api);
    }
}

void MegaApiImpl::fireOnReloadNeeded()
{
	for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
		(*it)->onReloadNeeded(api);

	for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
		(*it)->onReloadNeeded(api);
}

#ifdef ENABLE_SYNC
void MegaApiImpl::fireOnSyncStateChanged(MegaSyncPrivate *sync)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onSyncStateChanged(api, sync);

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ; it++)
        (*it)->onSyncStateChanged(api, sync);

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncStateChanged(api, sync);
    }
}

void MegaApiImpl::fireOnSyncEvent(MegaSyncPrivate *sync, MegaSyncEvent *event)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onSyncEvent(api, sync, event);

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ; it++)
        (*it)->onSyncEvent(api, sync, event);

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncEvent(api, sync, event);
    }

    delete event;
}

void MegaApiImpl::fireOnGlobalSyncStateChanged()
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onGlobalSyncStateChanged(api);

    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
        (*it)->onGlobalSyncStateChanged(api);
}

void MegaApiImpl::fireOnFileSyncStateChanged(MegaSyncPrivate *sync, const char *filePath, int newState)
{
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
        (*it)->onSyncFileStateChanged(api, sync, filePath, newState);

    for(set<MegaSyncListener *>::iterator it = syncListeners.begin(); it != syncListeners.end() ; it++)
        (*it)->onSyncFileStateChanged(api, sync, filePath, newState);

    MegaSyncListener* listener = sync->getListener();
    if(listener)
    {
        listener->onSyncFileStateChanged(api, sync, filePath, newState);
    }
}

#endif

#ifdef ENABLE_CHAT

void MegaApiImpl::fireOnChatsUpdate(MegaTextChatList *chats)
{
    for(set<MegaGlobalListener *>::iterator it = globalListeners.begin(); it != globalListeners.end() ; it++)
    {
        (*it)->onChatsUpdate(api, chats);
    }
    for(set<MegaListener *>::iterator it = listeners.begin(); it != listeners.end() ; it++)
    {
        (*it)->onChatsUpdate(api, chats);
    }
}

#endif

MegaError MegaApiImpl::checkAccess(MegaNode* megaNode, int level)
{
    if(!megaNode || level < MegaShare::ACCESS_UNKNOWN || level > MegaShare::ACCESS_OWNER)
    {
        return MegaError(API_EARGS);
    }

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
        return MegaError(API_ENOENT);
	}

    accesslevel_t a = OWNER;
    switch(level)
    {
    	case MegaShare::ACCESS_UNKNOWN:
    	case MegaShare::ACCESS_READ:
    		a = RDONLY;
    		break;
    	case MegaShare::ACCESS_READWRITE:
    		a = RDWR;
    		break;
    	case MegaShare::ACCESS_FULL:
    		a = FULL;
    		break;
    	case MegaShare::ACCESS_OWNER:
    		a = OWNER;
    		break;
    }

	MegaError e(client->checkaccess(node, a) ? API_OK : API_EACCESS);
    sdkMutex.unlock();

	return e;
}

MegaError MegaApiImpl::checkMove(MegaNode* megaNode, MegaNode* targetNode)
{
	if(!megaNode || !targetNode) return MegaError(API_EARGS);

    sdkMutex.lock();
    Node *node = client->nodebyhandle(megaNode->getHandle());
	Node *target = client->nodebyhandle(targetNode->getHandle());
	if(!node || !target)
	{
        sdkMutex.unlock();
        return MegaError(API_ENOENT);
	}

	MegaError e(client->checkmove(node,target));
    sdkMutex.unlock();

    return e;
}

bool MegaApiImpl::isFilesystemAvailable()
{
    sdkMutex.lock();
    bool result = client->nodebyhandle(client->rootnodes[0]) != NULL;
    sdkMutex.unlock();
    return result;
}

bool isDigit(const char *c)
{
    return (*c >= '0' && *c <= '9');
}

// returns 0 if i==j, +1 if i goes first, -1 if j goes first.
int naturalsorting_compare (const char *i, const char *j)
{
    static uint64_t maxNumber = (ULONG_MAX - 57) / 10; // 57 --> ASCII code for '9'

    bool stringMode = true;

    while (*i && *j)
    {
        if (stringMode)
        {
            char char_i, char_j;
            while ( (char_i = *i) && (char_j = *j) )
            {
                bool char_i_isDigit = isDigit(i);
                bool char_j_isDigit = isDigit(j);;

                if (char_i_isDigit && char_j_isDigit)
                {
                    stringMode = false;
                    break;
                }

                if(char_i_isDigit)
                {
                    return -1;
                }

                if(char_j_isDigit)
                {
                    return 1;
                }

                int difference = strncasecmp((char *)&char_i, (char *)&char_j, 1);
                if (difference)
                {
                    return difference;
                }

                ++i;
                ++j;
            }
        }
        else    // we are comparing numbers on both strings
        {
            uint64_t number_i = 0;
            unsigned int i_overflow_count = 0;
            while (*i && isDigit(i))
            {
                number_i = number_i * 10 + (*i - 48); // '0' ASCII code is 48
                ++i;

                // check the number won't overflow upon addition of next char
                if (number_i >= maxNumber)
                {
                    number_i -= maxNumber;
                    i_overflow_count++;
                }
            }

            uint64_t number_j = 0;
            unsigned int j_overflow_count = 0;
            while (*j && isDigit(j))
            {
                number_j = number_j * 10 + (*j - 48);
                ++j;

                // check the number won't overflow upon addition of next char
                if (number_j >= maxNumber)
                {
                    number_j -= maxNumber;
                    j_overflow_count++;
                }
            }

            int difference = i_overflow_count - j_overflow_count;
            if (difference)
            {
                return difference;
            }

            difference = number_i - number_j;
            if (difference)
            {
                return difference;
            }

            stringMode = true;
        }
    }

    if (*j)
    {
        return -1;
    }

    if (*i)
    {
        return 1;
    }

    return 0;
}

bool MegaApiImpl::nodeComparatorDefaultASC (Node *i, Node *j)
{
    if(i->type < j->type) return 0;
    if(i->type > j->type) return 1;

    if(naturalsorting_compare(i->displayname(), j->displayname())<=0) return 1;
	return 0;
}

bool MegaApiImpl::nodeComparatorDefaultDESC (Node *i, Node *j)
{
    if(i->type < j->type) return 1;
    if(i->type > j->type) return 0;
    if(naturalsorting_compare(i->displayname(), j->displayname())<=0) return 0;
    return 1;
}

bool MegaApiImpl::nodeComparatorSizeASC (Node *i, Node *j)
{ if(i->size < j->size) return 1; return 0;}
bool MegaApiImpl::nodeComparatorSizeDESC (Node *i, Node *j)
{ if(i->size < j->size) return 0; return 1;}

bool MegaApiImpl::nodeComparatorCreationASC  (Node *i, Node *j)
{ if(i->ctime < j->ctime) return 1; return 0;}
bool MegaApiImpl::nodeComparatorCreationDESC  (Node *i, Node *j)
{ if(i->ctime < j->ctime) return 0; return 1;}

bool MegaApiImpl::nodeComparatorModificationASC  (Node *i, Node *j)
{ if(i->mtime < j->mtime) return 1; return 0;}
bool MegaApiImpl::nodeComparatorModificationDESC  (Node *i, Node *j)
{ if(i->mtime < j->mtime) return 0; return 1;}

bool MegaApiImpl::nodeComparatorAlphabeticalASC  (Node *i, Node *j)
{ if(strcasecmp(i->displayname(), j->displayname())<=0) return 1; return 0; }
bool MegaApiImpl::nodeComparatorAlphabeticalDESC  (Node *i, Node *j)
{ if(strcasecmp(i->displayname(), j->displayname())<=0) return 0; return 1; }

int MegaApiImpl::getNumChildren(MegaNode* p)
{
	if (!p) return 0;

	sdkMutex.lock();
	Node *parent = client->nodebyhandle(p->getHandle());
	if (!parent)
	{
		sdkMutex.unlock();
		return 0;
	}

	int numChildren = parent->children.size();
	sdkMutex.unlock();

	return numChildren;
}

int MegaApiImpl::getNumChildFiles(MegaNode* p)
{
	if (!p) return 0;

	sdkMutex.lock();
	Node *parent = client->nodebyhandle(p->getHandle());
	if (!parent)
	{
		sdkMutex.unlock();
		return 0;
	}

	int numFiles = 0;
	for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); it++)
	{
		if ((*it)->type == FILENODE)
			numFiles++;
	}
	sdkMutex.unlock();

	return numFiles;
}

int MegaApiImpl::getNumChildFolders(MegaNode* p)
{
	if (!p) return 0;

	sdkMutex.lock();
	Node *parent = client->nodebyhandle(p->getHandle());
	if (!parent)
	{
		sdkMutex.unlock();
		return 0;
	}

	int numFolders = 0;
	for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); it++)
	{
		if ((*it)->type != FILENODE)
			numFolders++;
	}
	sdkMutex.unlock();

	return numFolders;
}


MegaNodeList *MegaApiImpl::getChildren(MegaNode* p, int order)
{
    if(!p) return new MegaNodeListPrivate();

    sdkMutex.lock();
    Node *parent = client->nodebyhandle(p->getHandle());
	if(!parent)
	{
        sdkMutex.unlock();
        return new MegaNodeListPrivate();
	}

    vector<Node *> childrenNodes;

    if(!order || order> MegaApi::ORDER_ALPHABETICAL_DESC)
	{
		for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); )
            childrenNodes.push_back(*it++);
	}
	else
	{
        bool (*comp)(Node*, Node*);
		switch(order)
		{
        case MegaApi::ORDER_DEFAULT_ASC: comp = MegaApiImpl::nodeComparatorDefaultASC; break;
        case MegaApi::ORDER_DEFAULT_DESC: comp = MegaApiImpl::nodeComparatorDefaultDESC; break;
        case MegaApi::ORDER_SIZE_ASC: comp = MegaApiImpl::nodeComparatorSizeASC; break;
        case MegaApi::ORDER_SIZE_DESC: comp = MegaApiImpl::nodeComparatorSizeDESC; break;
        case MegaApi::ORDER_CREATION_ASC: comp = MegaApiImpl::nodeComparatorCreationASC; break;
        case MegaApi::ORDER_CREATION_DESC: comp = MegaApiImpl::nodeComparatorCreationDESC; break;
        case MegaApi::ORDER_MODIFICATION_ASC: comp = MegaApiImpl::nodeComparatorModificationASC; break;
        case MegaApi::ORDER_MODIFICATION_DESC: comp = MegaApiImpl::nodeComparatorModificationDESC; break;
        case MegaApi::ORDER_ALPHABETICAL_ASC: comp = MegaApiImpl::nodeComparatorAlphabeticalASC; break;
        case MegaApi::ORDER_ALPHABETICAL_DESC: comp = MegaApiImpl::nodeComparatorAlphabeticalDESC; break;
        default: comp = MegaApiImpl::nodeComparatorDefaultASC; break;
		}

		for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); )
		{
            Node *n = *it++;
            vector<Node *>::iterator i = std::lower_bound(childrenNodes.begin(),
					childrenNodes.end(), n, comp);
            childrenNodes.insert(i, n);
		}
	}
    sdkMutex.unlock();

    if(childrenNodes.size()) return new MegaNodeListPrivate(childrenNodes.data(), childrenNodes.size());
    else return new MegaNodeListPrivate();
}

int MegaApiImpl::getIndex(MegaNode *n, int order)
{
    if(!n)
    {
        return -1;
    }

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
    if(!node)
    {
        sdkMutex.unlock();
        return -1;
    }

    Node *parent = node->parent;
    if(!parent)
    {
        sdkMutex.unlock();
        return -1;
    }


    if(!order || order> MegaApi::ORDER_ALPHABETICAL_DESC)
    {
        sdkMutex.unlock();
        return 0;
    }

    bool (*comp)(Node*, Node*);
    switch(order)
    {
        case MegaApi::ORDER_DEFAULT_ASC: comp = MegaApiImpl::nodeComparatorDefaultASC; break;
        case MegaApi::ORDER_DEFAULT_DESC: comp = MegaApiImpl::nodeComparatorDefaultDESC; break;
        case MegaApi::ORDER_SIZE_ASC: comp = MegaApiImpl::nodeComparatorSizeASC; break;
        case MegaApi::ORDER_SIZE_DESC: comp = MegaApiImpl::nodeComparatorSizeDESC; break;
        case MegaApi::ORDER_CREATION_ASC: comp = MegaApiImpl::nodeComparatorCreationASC; break;
        case MegaApi::ORDER_CREATION_DESC: comp = MegaApiImpl::nodeComparatorCreationDESC; break;
        case MegaApi::ORDER_MODIFICATION_ASC: comp = MegaApiImpl::nodeComparatorModificationASC; break;
        case MegaApi::ORDER_MODIFICATION_DESC: comp = MegaApiImpl::nodeComparatorModificationDESC; break;
        case MegaApi::ORDER_ALPHABETICAL_ASC: comp = MegaApiImpl::nodeComparatorAlphabeticalASC; break;
        case MegaApi::ORDER_ALPHABETICAL_DESC: comp = MegaApiImpl::nodeComparatorAlphabeticalDESC; break;
        default: comp = MegaApiImpl::nodeComparatorDefaultASC; break;
    }

    vector<Node *> childrenNodes;
    for (node_list::iterator it = parent->children.begin(); it != parent->children.end(); )
    {
        Node *temp = *it++;
        vector<Node *>::iterator i = std::lower_bound(childrenNodes.begin(),
                childrenNodes.end(), temp, comp);
        childrenNodes.insert(i, temp);
    }

    vector<Node *>::iterator i = std::lower_bound(childrenNodes.begin(),
            childrenNodes.end(), node, comp);

    sdkMutex.unlock();
    return i - childrenNodes.begin();
}

MegaNode *MegaApiImpl::getChildNode(MegaNode *parent, const char* name)
{
    if(!parent || !name)
    {
        return NULL;
    }

    sdkMutex.lock();
    Node *parentNode = client->nodebyhandle(parent->getHandle());
	if(!parentNode)
	{
        sdkMutex.unlock();
        return NULL;
	}

    MegaNode *node = MegaNodePrivate::fromNode(client->childnodebyname(parentNode, name));
    sdkMutex.unlock();
    return node;
}

Node *MegaApiImpl::getNodeByFingerprintInternal(const char *fingerprint)
{
    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return NULL;
    }

    sdkMutex.lock();
    Node *n  = client->nodebyfingerprint(fp);
    sdkMutex.unlock();

    delete fp;
    return n;
}

Node *MegaApiImpl::getNodeByFingerprintInternal(const char *fingerprint, Node *parent)
{

    FileFingerprint *fp = MegaApiImpl::getFileFingerprintInternal(fingerprint);
    if (!fp)
    {
        return NULL;
    }

    Node *n = NULL;
    sdkMutex.lock();
    node_vector *nodes = client->nodesbyfingerprint(fp);
    if (nodes->size())
    {
        n = nodes->at(0);
    }

    if (n && parent && n->parent != parent)
    {
        for (unsigned int i = 1; i < nodes->size(); i++)
        {
            Node* node = nodes->at(i);
            if (node->parent == parent)
            {
                n = node;
                break;
            }
        }
    }
    delete fp;
    delete nodes;
    sdkMutex.unlock();

    return n;
}

FileFingerprint *MegaApiImpl::getFileFingerprintInternal(const char *fingerprint)
{
    if(!fingerprint || !fingerprint[0])
    {
        return NULL;
    }

    m_off_t size = 0;
    unsigned int fsize = strlen(fingerprint);
    unsigned int ssize = fingerprint[0] - 'A';
    if(ssize > (sizeof(size) * 4 / 3 + 4) || fsize <= (ssize + 1))
    {
        return NULL;
    }

    int len =  sizeof(size) + 1;
    byte *buf = new byte[len];
    Base64::atob(fingerprint + 1, buf, len);
    int l = Serialize64::unserialize(buf, len, (uint64_t *)&size);
    delete [] buf;
    if(l <= 0)
    {
        return NULL;
    }

    string sfingerprint = fingerprint + ssize + 1;

    FileFingerprint *fp = new FileFingerprint;
    if(!fp->unserializefingerprint(&sfingerprint))
    {
        delete fp;
        return NULL;
    }

    fp->size = size;

    return fp;
}

MegaNode* MegaApiImpl::getParentNode(MegaNode* n)
{
    if(!n) return NULL;

    sdkMutex.lock();
    Node *node = client->nodebyhandle(n->getHandle());
	if(!node)
	{
        sdkMutex.unlock();
        return NULL;
	}

    MegaNode *result = MegaNodePrivate::fromNode(node->parent);
    sdkMutex.unlock();

	return result;
}

char* MegaApiImpl::getNodePath(MegaNode *node)
{
    if(!node) return NULL;

    sdkMutex.lock();
    Node *n = client->nodebyhandle(node->getHandle());
    if(!n)
	{
        sdkMutex.unlock();
        return NULL;
	}

	string path;
	if (n->nodehandle == client->rootnodes[0])
	{
		path = "/";
        sdkMutex.unlock();
        return stringToArray(path);
	}

	while (n)
	{
		switch (n->type)
		{
		case FOLDERNODE:
			path.insert(0,n->displayname());

			if (n->inshare)
			{
				path.insert(0,":");
				if (n->inshare->user) path.insert(0,n->inshare->user->email);
				else path.insert(0,"UNKNOWN");
                sdkMutex.unlock();
                return stringToArray(path);
			}
			break;

		case INCOMINGNODE:
			path.insert(0,"//in");
            sdkMutex.unlock();
            return stringToArray(path);

		case ROOTNODE:
            sdkMutex.unlock();
            return stringToArray(path);

		case RUBBISHNODE:
			path.insert(0,"//bin");
            sdkMutex.unlock();
            return stringToArray(path);

		case TYPE_UNKNOWN:
		case FILENODE:
			path.insert(0,n->displayname());
		}

		path.insert(0,"/");

        n = n->parent;
	}
    sdkMutex.unlock();
    return stringToArray(path);
}

MegaNode* MegaApiImpl::getNodeByPath(const char *path, MegaNode* node)
{
    if(!path) return NULL;

    sdkMutex.lock();
    Node *cwd = NULL;
    if(node) cwd = client->nodebyhandle(node->getHandle());

	vector<string> c;
	string s;
	int l = 0;
	const char* bptr = path;
	int remote = 0;
	Node* n;
	Node* nn;

	// split path by / or :
	do {
		if (!l)
		{
			if (*path >= 0)
			{
				if (*path == '\\')
				{
                    if (path > bptr)
                    {
                        s.append(bptr, path - bptr);
                    }

					bptr = ++path;

					if (*bptr == 0)
					{
						c.push_back(s);
						break;
					}

					path++;
					continue;
				}

				if (*path == '/' || *path == ':' || !*path)
				{
					if (*path == ':')
					{
						if (c.size())
						{
                            sdkMutex.unlock();
                            return NULL;
						}
						remote = 1;
					}

                    if (path > bptr)
                    {
                        s.append(bptr, path - bptr);
                    }

                    bptr = path + 1;

					c.push_back(s);

					s.erase();
				}
			}
            else if ((*path & 0xf0) == 0xe0)
            {
                l = 1;
            }
            else if ((*path & 0xf8) == 0xf0)
            {
                l = 2;
            }
            else if ((*path & 0xfc) == 0xf8)
            {
                l = 3;
            }
            else if ((*path & 0xfe) == 0xfc)
            {
                l = 4;
            }
		}
        else
        {
            l--;
        }
	} while (*path++);

	if (l)
	{
        sdkMutex.unlock();
        return NULL;
	}

	if (remote)
	{
        // target: user inbox - it's not a node - return NULL
		if (c.size() == 2 && !c[1].size())
		{
            sdkMutex.unlock();
            return NULL;
		}

		User* u;

        if ((u = client->finduser(c[0].c_str())))
        {
            // locate matching share from this user
            handle_set::iterator sit;
            string name;
            for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
            {
                if ((n = client->nodebyhandle(*sit)))
                {
                    if(!name.size())
                    {
                        name =  c[1];
                        n->client->fsaccess->normalize(&name);
                    }

                    if (!strcmp(name.c_str(), n->displayname()))
                    {
                        l = 2;
                        break;
                    }
                }
            }
        }

		if (!l)
		{
            sdkMutex.unlock();
            return NULL;
		}
	}
	else
	{
		// path starting with /
		if (c.size() > 1 && !c[0].size())
		{
			// path starting with //
			if (c.size() > 2 && !c[1].size())
			{
                if (c[2] == "in")
                {
                    n = client->nodebyhandle(client->rootnodes[1]);
                }
                else if (c[2] == "bin")
                {
                    n = client->nodebyhandle(client->rootnodes[2]);
                }
				else
				{
                    sdkMutex.unlock();
                    return NULL;
				}

				l = 3;
			}
			else
			{
				n = client->nodebyhandle(client->rootnodes[0]);
				l = 1;
			}
		}
        else
        {
            n = cwd;
        }
	}

	// parse relative path
	while (n && l < (int)c.size())
	{
		if (c[l] != ".")
		{
			if (c[l] == "..")
			{
                if (n->parent)
                {
                    n = n->parent;
                }
			}
			else
			{
				// locate child node (explicit ambiguity resolution: not implemented)
				if (c[l].size())
				{
                    nn = client->childnodebyname(n, c[l].c_str());

					if (!nn)
					{
                        sdkMutex.unlock();
                        return NULL;
					}

					n = nn;
				}
			}
		}

		l++;
	}

    MegaNode *result = MegaNodePrivate::fromNode(n);
    sdkMutex.unlock();
    return result;
}

MegaNode* MegaApiImpl::getNodeByHandle(handle handle)
{
	if(handle == UNDEF) return NULL;
    sdkMutex.lock();
    MegaNode *result = MegaNodePrivate::fromNode(client->nodebyhandle(handle));
    sdkMutex.unlock();
    return result;
}

MegaContactRequest *MegaApiImpl::getContactRequestByHandle(MegaHandle handle)
{
    sdkMutex.lock();
    if(client->pcrindex.find(handle) == client->pcrindex.end())
    {
        sdkMutex.unlock();
        return NULL;
    }
    MegaContactRequest* request = MegaContactRequestPrivate::fromContactRequest(client->pcrindex.at(handle));
    sdkMutex.unlock();
    return request;
}

void MegaApiImpl::sendPendingTransfers()
{
    MegaTransferPrivate *transfer;
    error e;
    int nextTag;

    while((transfer = transferQueue.pop()))
    {
        sdkMutex.lock();
        e = API_OK;
        nextTag = client->nextreqtag();

        switch(transfer->getType())
        {
            case MegaTransfer::TYPE_UPLOAD:
            {
                const char* localPath = transfer->getPath();
                const char* fileName = transfer->getFileName();
                int64_t mtime = transfer->getTime();
                Node *parent = client->nodebyhandle(transfer->getParentHandle());

                if(!localPath || !parent || !fileName || !(*fileName))
                {
                    e = API_EARGS;
                    break;
                }

                string tmpString = localPath;
                string wLocalPath;
                client->fsaccess->path2local(&tmpString, &wLocalPath);

                FileAccess *fa = fsAccess->newfileaccess();
                if(!fa->fopen(&wLocalPath, true, false))
                {
                    e = API_EREAD;
                    break;
                }

                nodetype_t type = fa->type;
                delete fa;

                if(type == FILENODE)
                {
                    currentTransfer = transfer;
                    string wFileName = fileName;
                    MegaFilePut *f = new MegaFilePut(client, &wLocalPath, &wFileName, transfer->getParentHandle(), "", mtime);
                    f->setTransfer(transfer);
                    bool started = client->startxfer(PUT, f, true);
                    if(!started)
                    {
                        if(!f->isvalid)
                        {
                            //Unable to read the file
                            transfer->setSyncTransfer(false);
                            transferMap[nextTag]=transfer;
                            transfer->setTag(nextTag);
                            fireOnTransferStart(transfer);
                            fireOnTransferFinish(transfer, MegaError(API_EREAD));
                        }
                        else
                        {
                            //Already existing transfer
                            transferMap[nextTag]=transfer;
                            transfer->setTag(nextTag);
                            fireOnTransferStart(transfer);
                            fireOnTransferFinish(transfer, MegaError(API_EEXIST));
                        }
                    }
                    else if(transfer->getTag() == -1)
                    {
                        //Already existing transfer
                        //Delete the new one and set the transfer as regular
                        transfer_map::iterator it = client->transfers[PUT].find(f);
                        if(it != client->transfers[PUT].end())
                        {
                            int previousTag = it->second->tag;
                            if(transferMap.find(previousTag) != transferMap.end())
                            {
                                MegaTransferPrivate* previousTransfer = transferMap.at(previousTag);
                                previousTransfer->setSyncTransfer(false);
                                delete transfer;
                            }
                        }
                    }
                    currentTransfer=NULL;
                }
                else
                {
                    transferMap[nextTag]=transfer;
                    transfer->setTag(nextTag);
                    MegaFolderUploadController *uploader = new MegaFolderUploadController(this, transfer);
                    uploader->start();
                }
                break;
            }
            case MegaTransfer::TYPE_DOWNLOAD:
            {
                Node *node = NULL;
                MegaNode *publicNode = transfer->getPublicNode();
                const char *parentPath = transfer->getParentPath();
                const char *fileName = transfer->getFileName();

                if (!publicNode)
                {
                    handle nodehandle = transfer->getNodeHandle();
                    node = client->nodebyhandle(nodehandle);
                }

                if (!node && !publicNode)
                {
                    e = API_ENOENT;
                    break;
                }

                if (!transfer->isStreamingTransfer() && !parentPath && !fileName)
                {
                    e = API_EARGS;
                    break;
                }

                if (!transfer->isStreamingTransfer() && node && node->type != FILENODE)
                {
                    // Folder download
                    transferMap[nextTag] = transfer;
                    transfer->setTag(nextTag);
                    MegaFolderDownloadController *downloader = new MegaFolderDownloadController(this, transfer);
                    downloader->start();
                    break;
                }

                // File download
                currentTransfer=transfer;
                if (!transfer->isStreamingTransfer())
                {
                    string name;
                    string securename;
                    string path;

					if(parentPath)
					{
						path = parentPath;
					}
					else
					{
						string separator;
						client->fsaccess->local2path(&client->fsaccess->localseparator, &separator);
						path = ".";
						path.append(separator);
					}

					MegaFileGet *f;

					if(node)
					{
						if(!fileName)
                        {
                            attr_map::iterator ait = node->attrs.map.find('n');
                            if(ait == node->attrs.map.end())
                            {
                                name = "CRYPTO_ERROR";
                            }
                            else if(!ait->second.size())
                            {
                                name = "BLANK";
                            }
                            else
                            {
                                name = ait->second;
                            }
                        }
                        else
                        {
                            name = fileName;
                        }

                        client->fsaccess->name2local(&name);
                        client->fsaccess->local2path(&name, &securename);
                        path += securename;
						f = new MegaFileGet(client, node, path);
					}
					else
					{
						if(!transfer->getFileName())
                            name = publicNode->getName();
                        else
                            name = transfer->getFileName();

                        client->fsaccess->name2local(&name);
                        client->fsaccess->local2path(&name, &securename);
                        path += securename;
						f = new MegaFileGet(client, publicNode, path);
					}

					transfer->setPath(path.c_str());
                    f->setTransfer(transfer);
                    bool ok = client->startxfer(GET, f, true);
                    if(transfer->getTag() == -1)
                    {
                        //Already existing transfer
                        if (ok)
                        {
                            //Set the transfer as regular
                            transfer_map::iterator it = client->transfers[GET].find(f);
                            if(it != client->transfers[GET].end())
                            {
                                int previousTag = it->second->tag;
                                if(transferMap.find(previousTag) != transferMap.end())
                                {
                                    MegaTransferPrivate* previousTransfer = transferMap.at(previousTag);
                                    previousTransfer->setSyncTransfer(false);
                                }
                            }
                        }
                        else
                        {
                            //Already existing transfer
                            transferMap[nextTag]=transfer;
                            transfer->setTag(nextTag);
                            fireOnTransferStart(transfer);

                            long long overquotaDelay = getBandwidthOverquotaDelay();
                            if (overquotaDelay)
                            {
                                fireOnTransferTemporaryError(transfer, MegaError(API_EOVERQUOTA, overquotaDelay));
                            }

                            fireOnTransferFinish(transfer, MegaError(API_EEXIST));
                        }
                    }
                }
                else
                {
                	m_off_t startPos = transfer->getStartPos();
                	m_off_t endPos = transfer->getEndPos();
                	if(startPos < 0 || endPos < 0 || startPos > endPos) { e = API_EARGS; break; }
                	if(node)
                	{
                        transfer->setFileName(node->displayname());
                		if(startPos >= node->size || endPos >= node->size)
                		{ e = API_EARGS; break; }

                		m_off_t totalBytes = endPos - startPos + 1;
                	    transferMap[nextTag]=transfer;
						transfer->setTotalBytes(totalBytes);
						transfer->setTag(nextTag);
                        fireOnTransferStart(transfer);
                	    client->pread(node, startPos, totalBytes, transfer);
                	    waiter->notify();
                	}
                	else
                	{
                        transfer->setFileName(publicNode->getName());
                        if(startPos >= publicNode->getSize() || endPos >= publicNode->getSize())
                        { e = API_EARGS; break; }

                        m_off_t totalBytes = endPos - startPos + 1;
                        transferMap[nextTag]=transfer;
                        transfer->setTotalBytes(totalBytes);
                        transfer->setTag(nextTag);
                        fireOnTransferStart(transfer);
                        SymmCipher cipher;
                        cipher.setkey(publicNode->getNodeKey());
                        client->pread(publicNode->getHandle(), &cipher,
                            MemAccess::get<int64_t>((const char*)publicNode->getNodeKey()->data() + SymmCipher::KEYLENGTH),
                                      startPos, totalBytes, transfer);
                        waiter->notify();
                	}
                }

                currentTransfer=NULL;
				break;
			}
		}

		if(e)
            fireOnTransferFinish(transfer, MegaError(e));

        sdkMutex.unlock();
    }
}

void MegaApiImpl::removeRecursively(const char *path)
{
#ifndef _WIN32
    string spath = path;
    PosixFileSystemAccess::emptydirlocal(&spath);
#else
    string utf16path;
    MegaApi::utf8ToUtf16(path, &utf16path);
    if(utf16path.size())
    {
        utf16path.resize(utf16path.size()-2);
        WinFileSystemAccess::emptydirlocal(&utf16path);
    }
#endif
}


void MegaApiImpl::sendPendingRequests()
{
	MegaRequestPrivate *request;
	error e;
    int nextTag = 0;

	while((request = requestQueue.pop()))
	{
        if (!nextTag && request->getType() != MegaRequest::TYPE_LOGOUT)
        {
            client->abortbackoff(false);
        }

		sdkMutex.lock();
		nextTag = client->nextreqtag();
        request->setTag(nextTag);
		requestMap[nextTag]=request;
		e = API_OK;

        fireOnRequestStart(request);
		switch(request->getType())
		{
		case MegaRequest::TYPE_LOGIN:
		{
			const char *login = request->getEmail();
			const char *password = request->getPassword();
            const char* megaFolderLink = request->getLink();
            const char* base64pwkey = request->getPrivateKey();
            const char* sessionKey = request->getSessionKey();

            if(!megaFolderLink && (!(login && password)) && !sessionKey && (!(login && base64pwkey)))
            {
                e = API_EARGS;
                break;
            }

            string slogin;
            if(login)
            {
                slogin = login;
                slogin.erase(slogin.begin(), std::find_if(slogin.begin(), slogin.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
                slogin.erase(std::find_if(slogin.rbegin(), slogin.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), slogin.end());
            }

            requestMap.erase(request->getTag());
            while(!requestMap.empty())
            {
                std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
                if(it->second) fireOnRequestFinish(it->second, MegaError(MegaError::API_EACCESS));
            }

            while(!transferMap.empty())
            {
                std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
                if(it->second) fireOnTransferFinish(it->second, MegaError(MegaError::API_EACCESS));
            }
            requestMap[request->getTag()]=request;

            if(sessionKey)
            {
                byte session[MAX_SESSION_LENGTH];
                int size = Base64::atob(sessionKey, (byte *)session, sizeof session);
                client->login(session, size);
            }
            else if(login && base64pwkey)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                Base64::atob(base64pwkey, (byte *)pwkey, sizeof pwkey);

                if(password)
                {
                    uint64_t emailhash;
                    Base64::atob(password, (byte *)&emailhash, sizeof emailhash);
                    client->fastlogin(slogin.c_str(), pwkey, emailhash);
                }
                else
                {
                    client->login(slogin.c_str(), pwkey);
                }
            }
            else if(login && password)
            {
                byte pwkey[SymmCipher::KEYLENGTH];
                if((e = client->pw_key(password,pwkey))) break;
                client->login(slogin.c_str(), pwkey);
            }
            else
            {
                e = client->folderaccess(megaFolderLink);
                if(e == API_OK)
                {
                    fireOnRequestFinish(request, MegaError(e));
                }
            }

            break;
		}
        case MegaRequest::TYPE_CREATE_FOLDER:
		{
			Node *parent = client->nodebyhandle(request->getParentHandle());
			const char *name = request->getName();
            if(!name || !(*name) || !parent) { e = API_EARGS; break; }

			NewNode *newnode = new NewNode[1];
			SymmCipher key;
			string attrstring;
			byte buf[FOLDERNODEKEYLENGTH];

			// set up new node as folder node
			newnode->source = NEW_NODE;
			newnode->type = FOLDERNODE;
			newnode->nodehandle = 0;
			newnode->parenthandle = UNDEF;

			// generate fresh random key for this folder node
			PrnGen::genblock(buf,FOLDERNODEKEYLENGTH);
			newnode->nodekey.assign((char*)buf,FOLDERNODEKEYLENGTH);
			key.setkey(buf);

			// generate fresh attribute object with the folder name
			AttrMap attrs;
            string sname = name;
            fsAccess->normalize(&sname);
            attrs.map['n'] = sname;

			// JSON-encode object and encrypt attribute string
			attrs.getjson(&attrstring);
            newnode->attrstring = new string;
            client->makeattr(&key,newnode->attrstring,attrstring.c_str());

			// add the newly generated folder node
			client->putnodes(parent->nodehandle,newnode,1);
			break;
		}
		case MegaRequest::TYPE_MOVE:
		{
			Node *node = client->nodebyhandle(request->getNodeHandle());
			Node *newParent = client->nodebyhandle(request->getParentHandle());
			if(!node || !newParent) { e = API_EARGS; break; }

            if(node->parent == newParent)
            {
                fireOnRequestFinish(request, MegaError(API_OK));
                break;
            }

            if ((e = client->checkmove(node, newParent)))
            {
                if (!client->checkaccess(newParent, RDWR))
                {
                    break;
                }

                unsigned nc;
                TreeProcCopy tc;

                // determine number of nodes to be copied
                client->proctree(node, &tc);
                tc.allocnodes();
                nc = tc.nc;

                // build new nodes array
                client->proctree(node, &tc);
                if (!nc)
                {
                    e = API_EARGS;
                    break;
                }

                tc.nn->parenthandle = UNDEF;
                client->putnodes(newParent->nodehandle, tc.nn, nc);
                e = API_OK;
                break;
            }

			e = client->rename(node, newParent);
			break;
		}
		case MegaRequest::TYPE_COPY:
		{
			Node *node = client->nodebyhandle(request->getNodeHandle());
			Node *target = client->nodebyhandle(request->getParentHandle());
			const char* email = request->getEmail();
            MegaNode *publicNode = request->getPublicNode();
            const char *newName = request->getName();

            if (!publicNode || (!target && !email) || (newName && !(*newName))) { e = API_EARGS; break; }

            if (!node)
            {
                NewNode *newnode = new NewNode[1];
                newnode->nodekey.assign(publicNode->getNodeKey()->data(), publicNode->getNodeKey()->size());
                newnode->attrstring = new string;

                if (publicNode->isPublic())
                {
                    newnode->attrstring->assign(publicNode->getAttrString()->data(), publicNode->getAttrString()->size());
                    newnode->source = NEW_PUBLIC;
                }
                else
                {
                    SymmCipher key;
                    AttrMap attrs;

                    key.setkey((const byte*)publicNode->getNodeKey()->data(), FILENODE);
                    string sname = publicNode->getName();
                    fsAccess->normalize(&sname);
                    attrs.map['n'] = sname;

                    const char *fingerprint = publicNode->getFingerprint();
                    if (fingerprint && fingerprint[0])
                    {
                        m_off_t size = 0;
                        unsigned int fsize = strlen(fingerprint);
                        unsigned int ssize = fingerprint[0] - 'A';
                        if (!(ssize > (sizeof(size) * 4 / 3 + 4) || fsize <= (ssize + 1)))
                        {
                            int len =  sizeof(size) + 1;
                            byte *buf = new byte[len];
                            Base64::atob(fingerprint + 1, buf, len);
                            int l = Serialize64::unserialize(buf, len, (uint64_t *)&size);
                            delete [] buf;
                            if (l > 0)
                            {
                                attrs.map['c'] = fingerprint + ssize + 1;
                            }
                        }
                    }

                    string attrstring;
                    attrs.getjson(&attrstring);
                    client->makeattr(&key,newnode->attrstring, attrstring.c_str());
                    newnode->source = NEW_NODE;
                }

                newnode->nodehandle = publicNode->getHandle();
                newnode->type = FILENODE;
                newnode->parenthandle = UNDEF;

                if(target)
                {
                    client->putnodes(target->nodehandle, newnode, 1);
                }
                else
                {
                    client->putnodes(email, newnode, 1);
                }
            }
            else
            {
                unsigned nc;
                TreeProcCopy tc;

                // determine number of nodes to be copied
                client->proctree(node,&tc);
                tc.allocnodes();
                nc = tc.nc;

                // build new nodes array
                client->proctree(node,&tc);
                if (!nc)
                {
                    e = API_EARGS;
                    break;
                }

                tc.nn->parenthandle = UNDEF;

                if(nc == 1 && newName && tc.nn[0].nodekey.size())
                {
                    SymmCipher key;
                    AttrMap attrs;
                    string attrstring;

                    key.setkey((const byte*)tc.nn[0].nodekey.data(), node->type);
                    attrs = node->attrs;

                    string sname = newName;
                    fsAccess->normalize(&sname);
                    attrs.map['n'] = sname;

                    attrs.getjson(&attrstring);
                    client->makeattr(&key,tc.nn[0].attrstring, attrstring.c_str());
                }

                if (target)
                {
                    client->putnodes(target->nodehandle,tc.nn,nc);
                }
                else
                {
                    client->putnodes(email, tc.nn, nc);
                }
            }
			break;
		}
        case MegaRequest::TYPE_RENAME:
        {
            Node* node = client->nodebyhandle(request->getNodeHandle());
            const char* newName = request->getName();
            if(!node || !newName || !(*newName)) { e = API_EARGS; break; }

            if (!client->checkaccess(node,FULL)) { e = API_EACCESS; break; }

            string sname = newName;
            fsAccess->normalize(&sname);
            node->attrs.map['n'] = sname;
            e = client->setattr(node);
            break;
        }
		case MegaRequest::TYPE_REMOVE:
		{
			Node* node = client->nodebyhandle(request->getNodeHandle());
			if(!node) { e = API_EARGS; break; }

			e = client->unlink(node);
			break;
		}
		case MegaRequest::TYPE_SHARE:
		{
			Node *node = client->nodebyhandle(request->getNodeHandle());
			const char* email = request->getEmail();
			int access = request->getAccess();
            if(!node || !email || !strchr(email, '@'))
            {
                e = API_EARGS;
                break;
            }

            accesslevel_t a;
			switch(access)
			{
				case MegaShare::ACCESS_UNKNOWN:
                    a = ACCESS_UNKNOWN;
                    break;
				case MegaShare::ACCESS_READ:
					a = RDONLY;
					break;
				case MegaShare::ACCESS_READWRITE:
					a = RDWR;
					break;
				case MegaShare::ACCESS_FULL:
					a = FULL;
					break;
				case MegaShare::ACCESS_OWNER:
					a = OWNER;
					break;
                default:
                    e = API_EARGS;
			}

            if(e == API_OK)
                client->setshare(node, email, a);
			break;
		}
		case MegaRequest::TYPE_IMPORT_LINK:
		case MegaRequest::TYPE_GET_PUBLIC_NODE:
		{
			Node *node = client->nodebyhandle(request->getParentHandle());
			const char* megaFileLink = request->getLink();
			if(!megaFileLink) { e = API_EARGS; break; }
			if((request->getType()==MegaRequest::TYPE_IMPORT_LINK) && (!node)) { e = API_EARGS; break; }

			e = client->openfilelink(megaFileLink, 1);
			break;
		}
		case MegaRequest::TYPE_EXPORT:
		{
			Node* node = client->nodebyhandle(request->getNodeHandle());
			if(!node) { e = API_EARGS; break; }

            e = client->exportnode(node, !request->getAccess(), request->getNumber());
			break;
		}
		case MegaRequest::TYPE_FETCH_NODES:
		{
			client->fetchnodes();
			break;
		}
		case MegaRequest::TYPE_ACCOUNT_DETAILS:
		{
            if(client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

			int numDetails = request->getNumDetails();
			bool storage = (numDetails & 0x01) != 0;
			bool transfer = (numDetails & 0x02) != 0;
			bool pro = (numDetails & 0x04) != 0;
			bool transactions = (numDetails & 0x08) != 0;
			bool purchases = (numDetails & 0x10) != 0;
			bool sessions = (numDetails & 0x20) != 0;

			numDetails = 1;
			if(transactions) numDetails++;
			if(purchases) numDetails++;
			if(sessions) numDetails++;

			request->setNumDetails(numDetails);

			client->getaccountdetails(request->getAccountDetails(), storage, transfer, pro, transactions, purchases, sessions);
			break;
		}
		case MegaRequest::TYPE_CHANGE_PW:
		{
			const char* oldPassword = request->getPassword();
			const char* newPassword = request->getNewPassword();
			if(!oldPassword || !newPassword) { e = API_EARGS; break; }

			byte pwkey[SymmCipher::KEYLENGTH];
			byte newpwkey[SymmCipher::KEYLENGTH];
			if((e = client->pw_key(oldPassword, pwkey))) { e = API_EARGS; break; }
			if((e = client->pw_key(newPassword, newpwkey))) { e = API_EARGS; break; }
			e = client->changepw(pwkey, newpwkey);
			break;
		}
		case MegaRequest::TYPE_LOGOUT:
		{
            if (request->getParamType() == API_ESSL && client->retryessl)
            {
                e = API_EINCOMPLETE;
                break;
            }

            if(request->getFlag())
            {
                client->logout();
            }
            else
            {
                client->locallogout();
                client->restag = nextTag;
                logout_result(API_OK);
            }
			break;
		}
		case MegaRequest::TYPE_GET_ATTR_FILE:
		{
			const char* dstFilePath = request->getFile();
            int type = request->getParamType();
			Node *node = client->nodebyhandle(request->getNodeHandle());

			if(!dstFilePath || !node) { e = API_EARGS; break; }

            e = client->getfa(node, (fatype)type);
            if(e == API_EEXIST)
            {
                e = API_OK;
                int prevtag = client->restag;
                MegaRequestPrivate* req = NULL;
                while(prevtag)
                {
                    if(requestMap.find(prevtag) == requestMap.end())
                    {
                        LOG_err << "Invalid duplicate getattr request";
                        req = NULL;
                        e = API_EINTERNAL;
                        break;
                    }

                    req = requestMap.at(prevtag);
                    if(!req || (req->getType() != MegaRequest::TYPE_GET_ATTR_FILE))
                    {
                        LOG_err << "Invalid duplicate getattr type";
                        req = NULL;
                        e = API_EINTERNAL;
                        break;
                    }

                    prevtag = req->getNumber();
                }

                if(req)
                {
                    LOG_debug << "Duplicate getattr detected";
                    req->setNumber(request->getTag());
                }
            }
			break;
		}
		case MegaRequest::TYPE_GET_ATTR_USER:
		{
            const char* value = request->getFile();
            int type = request->getParamType();
            const char *email = request->getEmail();

            User *user;
            if(email)
            {
                user = client->finduser(email, 0);
            }
            else
            {
                user = client->finduser(client->me, 0);
            }

            if(!user)
            {
                e = API_EARGS;
                break;
            }

            string attrname;
            switch(type)
            {
                case MegaApi::USER_ATTR_AVATAR:
                {
                    if (!value)    // destination file
                    {
                        e = API_EARGS;
                        break;
                    }

                    attrname = "+a";
                    break;
                }

                case MegaApi::USER_ATTR_FIRSTNAME:
                {
                    attrname = "firstname";
                    break;
                }

                case MegaApi::USER_ATTR_LASTNAME:
                {
                    attrname = "lastname";
                    break;
                }

                case MegaApi::USER_ATTR_AUTHRING:
                {
                    attrname = "*!authring";
                    break;
                }

                case MegaApi::USER_ATTR_LAST_INTERACTION:
                {
                    attrname = "*!lstint";
                    break;
                }

                default:
                {
                    e = API_EARGS;
                    break;
                }
            }

            if(!e)
            {
                client->getua(user, attrname.c_str());
            }

            break;
		}
		case MegaRequest::TYPE_SET_ATTR_USER:
		{
            const char* file = request->getFile();
            const char* value = request->getText();
            int type = request->getParamType();

            if (!value && type != MegaApi::USER_ATTR_AVATAR)
            {
                e = API_EARGS;
                break;
            }

            string attrname;
            string attrvalue;

            switch (type)
            {
                case MegaApi::USER_ATTR_AVATAR:
                {
                    attrname = "+a";

                    if (file)
                    {
                        string path = file;
                        string localpath;
                        fsAccess->path2local(&path, &localpath);

                        FileAccess *f = fsAccess->newfileaccess();
                        if (!f->fopen(&localpath, 1, 0))
                        {
                            delete f;
                            e = API_EREAD;
                            break;
                        }

                        if (!f->fread(&attrvalue, f->size, 0, 0))
                        {
                            delete f;
                            e = API_EREAD;
                            break;
                        }
                        delete f;
                    }
                    break;
                }

                case MegaApi::USER_ATTR_FIRSTNAME:
                {
                    attrname = "firstname";
                    attrvalue = value;
                    break;
                }

                case MegaApi::USER_ATTR_LASTNAME:
                {
                    attrname = "lastname";
                    attrvalue = value;
                    break;
                }

                case MegaApi::USER_ATTR_AUTHRING:
                {
                    attrname = "*!authring";
                    attrvalue = value;
                    break;
                }

                case MegaApi::USER_ATTR_LAST_INTERACTION:
                {
                    attrname = "*!lstint";
                    attrvalue = value;
                    break;
                }

                default:
                {
                    e = API_EARGS;
                    break;
                }
            }

            if (!e)
            {
                if ((type == MegaApi::USER_ATTR_AVATAR) && (attrvalue.empty()))
                {
                    client->putua(attrname.c_str());
                }
                else
                {
                    client->putua(attrname.c_str(), (byte *)attrvalue.data(), attrvalue.size());
                }
            }

            break;
		}
        case MegaRequest::TYPE_SET_ATTR_FILE:
        {
            const char* srcFilePath = request->getFile();
            int type = request->getParamType();
            Node *node = client->nodebyhandle(request->getNodeHandle());

            if(!srcFilePath || !node) { e = API_EARGS; break; }

            string path = srcFilePath;
            string localpath;
            fsAccess->path2local(&path, &localpath);

            string *attributedata = new string;
            FileAccess *f = fsAccess->newfileaccess();
            if (!f->fopen(&localpath, 1, 0))
            {
                delete f;
                delete attributedata;
                e = API_EREAD;
                break;
            }

            if(!f->fread(attributedata, f->size, 0, 0))
            {
                delete f;
                delete attributedata;
                e = API_EREAD;
                break;
            }
            delete f;

            client->putfa(node->nodehandle, (fatype)type, node->nodecipher(), attributedata);
            //attributedata is not deleted because putfa takes its ownership
            break;
        }
        case MegaRequest::TYPE_SET_ATTR_NODE:
        {
            Node *node = client->nodebyhandle(request->getNodeHandle());
            bool isOfficial = request->getFlag();

            if (!node)
            {
                e = API_EARGS;
                break;
            }

            if (!client->checkaccess(node, FULL))
            {
                e = API_EACCESS;
                break;
            }

            if (isOfficial)
            {
                int type = request->getParamType();

                if (type == MegaApi::NODE_ATTR_DURATION)
                {
                    int secs = request->getNumber();
                    if (node->type != FILENODE || secs < MegaNode::INVALID_DURATION)
                    {
                        e = API_EARGS;
                        break;
                    }

                    if (secs == MegaNode::INVALID_DURATION)
                    {
                        node->attrs.map.erase('d');
                    }
                    else
                    {
                        string buf;
                        buf.assign((char*)&secs, sizeof secs);
                        node->attrs.map['d'] = buf;
                    }
                }
                else if (type == MegaApi::NODE_ATTR_COORDINATES)
                {
                    if (node->type != FILENODE)
                    {
                        e = API_EARGS;
                        break;
                    }

                    string coordsValue = request->getText();
                    nameid coordsName = AttrMap::string2nameid("gps");

                    size_t separator = coordsValue.find_first_of(';');
                    if ((separator == coordsValue.npos) || (separator + 1 >= coordsValue.size()))
                    {
                        e = API_EARGS;
                        break;
                    }

                    double latValue = MegaNode::INVALID_COORDINATE;
                    double lonValue = MegaNode::INVALID_COORDINATE;

                    const char *ptr = coordsValue.substr(0, separator).c_str();
                    char *endptr = NULL;
                    latValue = std::strtod(ptr, &endptr);
                    if ((latValue == 0 && endptr == ptr) || (latValue == HUGE_VAL))
                    {
                        e = API_EARGS;
                        break;
                    }

                    ptr = coordsValue.substr(separator+1, coordsValue.length()).c_str();
                    endptr = NULL;
                    lonValue = std::strtod(ptr, &endptr);
                    if ((lonValue == 0 && endptr == ptr) || (lonValue == HUGE_VAL))
                    {
                        e = API_EARGS;
                        break;
                    }

                    if (lonValue == MegaNode::INVALID_COORDINATE &&
                            latValue == MegaNode::INVALID_COORDINATE)
                    {
                        node->attrs.map.erase(coordsName);
                    }
                    else
                    {
                        if (lonValue < -180 || lonValue > +180 ||
                                latValue < -90 || latValue > +90)
                        {
                            e = API_EARGS;
                            break;
                        }

                        node->attrs.map[coordsName] = coordsValue;
                    }
                }
                else
                {
                    e = API_EARGS;
                    break;
                }
            }
            else    // custom attribute, not official
            {
                const char* attrName = request->getName();
                const char* attrValue = request->getText();

                if (!attrName || !attrName[0] || strlen(attrName) > 7)
                {
                    e = API_EARGS;
                    break;
                }

                string sname = attrName;
                fsAccess->normalize(&sname);
                sname.insert(0, "_");
                nameid attr = AttrMap::string2nameid(sname.c_str());

                if (attrValue)
                {
                    string svalue = attrValue;
                    fsAccess->normalize(&svalue);
                    node->attrs.map[attr] = svalue;
                }
                else
                {
                    node->attrs.map.erase(attr);
                }
            }

            if (!e)
            {
                e = client->setattr(node);
            }

            break;
        }
		case MegaRequest::TYPE_CANCEL_ATTR_FILE:
		{
			int type = request->getParamType();
			Node *node = client->nodebyhandle(request->getNodeHandle());

			if (!node) { e = API_EARGS; break; }

            e = client->getfa(node, (fatype)type, 1);
			if (!e)
			{
				std::map<int, MegaRequestPrivate*>::iterator it = requestMap.begin();
				while(it != requestMap.end())
				{
					MegaRequestPrivate *r = it->second;
					it++;
					if (r->getType() == MegaRequest::TYPE_GET_ATTR_FILE &&
						r->getParamType() == request->getParamType() &&
						r->getNodeHandle() == request->getNodeHandle())
					{
						fireOnRequestFinish(r, MegaError(API_EINCOMPLETE));
					}
				}
				fireOnRequestFinish(request, MegaError(e));
			}
			break;
		}
		case MegaRequest::TYPE_RETRY_PENDING_CONNECTIONS:
		{
			bool disconnect = request->getFlag();
			bool includexfers = request->getNumber();
			client->abortbackoff(includexfers);
			if(disconnect)
            {
                client->disconnect();

#if defined(WINDOWS_PHONE) || TARGET_OS_IPHONE
                // Workaround to get the IP of valid DNS servers on Windows Phone/iOS
                string servers;

                while (true)
                {
                #ifdef WINDOWS_PHONE
                    client->httpio->getMEGADNSservers(&servers);
                #else
                    __res_state res;
                    if(res_ninit(&res) == 0)
                    {
                        union res_sockaddr_union u[MAXNS];
                        int nscount = res_getservers(&res, u, MAXNS);

                        for(int i = 0; i < nscount; i++)
                        {
                            char straddr[INET6_ADDRSTRLEN];
                            straddr[0] = 0;

                            if(u[i].sin.sin_family == PF_INET)
                            {
                                mega_inet_ntop(PF_INET, &u[i].sin.sin_addr, straddr, sizeof(straddr));
                            }

                            if(u[i].sin6.sin6_family == PF_INET6)
                            {
                                mega_inet_ntop(PF_INET6, &u[i].sin6.sin6_addr, straddr, sizeof(straddr));
                            }

                            if(straddr[0])
                            {
                                if (servers.size())
                                {
                                    servers.append(",");
                                }
                                servers.append(straddr);
                            }
                        }

                        res_ndestroy(&res);
                    }
                #endif

                    if (servers.size())
                        break;

                #ifdef WINDOWS_PHONE
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                #else
                    sleep(1);
                #endif
                }

                LOG_debug << "Using MEGA DNS servers " << servers;
                httpio->setdnsservers(servers.c_str());
#endif
            }

			fireOnRequestFinish(request, MegaError(API_OK));
			break;
        }
        case MegaRequest::TYPE_INVITE_CONTACT:
        {
            const char *email = request->getEmail();
            const char *message = request->getText();
            int action = request->getNumber();

            if(client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            if(!email || !client->finduser(client->me)->email.compare(email))
            {
                e = API_EARGS;
                break;
            }

            if (action != OPCA_ADD && action != OPCA_REMIND && action != OPCA_DELETE)
            {
                e = API_EARGS;
                break;
            }

            client->setpcr(email, (opcactions_t)action, message);
            break;
        }
        case MegaRequest::TYPE_REPLY_CONTACT_REQUEST:
        {
            handle h = request->getNodeHandle();
            int action = request->getNumber();

            if(h == INVALID_HANDLE || action < 0 || action > MegaContactRequest::REPLY_ACTION_IGNORE)
            {
                e = API_EARGS;
                break;
            }

            client->updatepcr(h, (ipcactions_t)action);
            break;
        }
		case MegaRequest::TYPE_REMOVE_CONTACT:
		{
			const char *email = request->getEmail();
			if(!email) { e = API_EARGS; break; }
            e = client->removecontact(email, HIDDEN);
			break;
		}
		case MegaRequest::TYPE_CREATE_ACCOUNT:
		{
			const char *email = request->getEmail();
			const char *password = request->getPassword();
            const char *name = request->getName();
            const char *pwkey = request->getPrivateKey();

            if(!email || !name || (!password && !pwkey))
			{
				e = API_EARGS; break;
			}

            requestMap.erase(request->getTag());
            while(!requestMap.empty())
            {
                std::map<int,MegaRequestPrivate*>::iterator it=requestMap.begin();
                if(it->second) fireOnRequestFinish(it->second, MegaError(MegaError::API_EACCESS));
            }

            while(!transferMap.empty())
            {
                std::map<int, MegaTransferPrivate *>::iterator it=transferMap.begin();
                if(it->second) fireOnTransferFinish(it->second, MegaError(MegaError::API_EACCESS));
            }
            requestMap[request->getTag()]=request;

			client->createephemeral();
			break;
		}
        case MegaRequest::TYPE_QUERY_SIGNUP_LINK:
        {
            const char *link = request->getLink();
            if(!link)
            {
                e = API_EARGS;
                break;
            }

            const char* ptr = link;
            const char* tptr;

            if ((tptr = strstr(ptr,"#confirm")))
            {
                ptr = tptr+8;

                unsigned len = (strlen(link)-(ptr-link))*3/4+4;
                byte *c = new byte[len];
                len = Base64::atob(ptr,c,len);
                if (len)
                {
                    client->querysignuplink(c,len);
                }
                else
                {
                    e = API_EARGS;
                }
                delete[] c;
                break;
            }
            else if ((tptr = strstr(ptr,"#newsignup")))
            {
                ptr = tptr+10;

                unsigned len = (strlen(link)-(ptr-link))*3/4+4;
                byte *c = new byte[len];
                len = Base64::atob(ptr,c,len);

                if (len > 8)
                {
                    // extract email and email_hash from link
                    byte *email = c;
                    byte *sha512bytes = c+len-8;    // last 11 chars

                    // get the hash for the received email
                    Hash sha512;
                    sha512.add(email, len-8);
                    string sha512str;
                    sha512.get(&sha512str);

                    // and finally check it
                    if (memcmp(sha512bytes, sha512str.data(), 8) == 0)
                    {
                        email[len-8] = '\0';
                        request->setEmail((const char *)email);
                        delete[] c;

                        fireOnRequestFinish(request, MegaError(API_OK));
                        break;
                    }
                }

                delete[] c;
            }

            e = API_EARGS;
            break;
        }
		case MegaRequest::TYPE_CONFIRM_ACCOUNT:
		{
			const char *link = request->getLink();
			const char *password = request->getPassword();
			const char *pwkey = request->getPrivateKey();

            if(!link || (request->getType() == MegaRequest::TYPE_CONFIRM_ACCOUNT && !password && !pwkey))
			{
				e = API_EARGS;
				break;
			}

			const char* ptr = link;
			const char* tptr;

			if ((tptr = strstr(ptr,"#confirm"))) ptr = tptr+8;

			unsigned len = (strlen(link)-(ptr-link))*3/4+4;
			byte *c = new byte[len];
            len = Base64::atob(ptr,c,len);
            if (len)
            {
                client->querysignuplink(c,len);
            }
            else
            {
                e = API_EARGS;
            }
			delete[] c;
			break;
		}
        case MegaRequest::TYPE_GET_RECOVERY_LINK:
        {
            const char *email = request->getEmail();
            bool hasMasterKey = request->getFlag();

            if (!email || !email[0])
            {
                e = API_EARGS;
                break;
            }

            client->getrecoverylink(email, hasMasterKey);
            break;
        }
        case MegaRequest::TYPE_QUERY_RECOVERY_LINK:
        {
            const char *link = request->getLink();

            const char* code;
            if (link && (code = strstr(link, "#recover")))
            {
                code += strlen("#recover");
            }
            else if (link && (code = strstr(link, "#verify")))
            {
                code += strlen("#verify");
            }
            else
            {
                e = API_EARGS;
                break;
            }

            client->queryrecoverylink(code);
            break;
        }
        case MegaRequest::TYPE_CONFIRM_RECOVERY_LINK:
        {
            const char *link = request->getLink();
            const char *newPwd = request->getPassword();

            const char* code;
            if (newPwd && link && (code = strstr(link, "#recover")))
            {
                code += strlen("#recover");
            }
            else
            {
                e = API_EARGS;
                break;
            }

            // concatenate query + confirm requests
            client->queryrecoverylink(code);
            break;
        }
        case MegaRequest::TYPE_GET_CANCEL_LINK:
        {
            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            User *u = client->finduser(client->me);
            if (!u)
            {
                e = API_ENOENT;
                break;
            }

            client->getcancellink(u->email.c_str());
            break;
        }
        case MegaRequest::TYPE_CONFIRM_CANCEL_LINK:
        {
            const char *link = request->getLink();
            const char *pwd = request->getPassword();

            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
            }

            const char* code;
            if (!pwd || !link || !(code = strstr(link, "#cancel")))
            {
                e = API_EARGS;
                break;
            }

            byte pwkey[SymmCipher::KEYLENGTH];
            client->pw_key(pwd, pwkey);

            // concatenate login + confirm requests
            e = client->validatepwd(pwkey);
            break;
        }
        case MegaRequest::TYPE_GET_CHANGE_EMAIL_LINK:
        {
            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            const char *email = request->getEmail();
            if (!email)
            {
                e = API_EARGS;
                break;
            }

            client->getemaillink(email);
            break;
        }
        case MegaRequest::TYPE_CONFIRM_CHANGE_EMAIL_LINK:
        {
            const char *link = request->getLink();
            const char *pwd = request->getPassword();

            if (client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
            }

            const char* code;
            if (pwd && link && (code = strstr(link, "#verify")))
            {
                code += strlen("#verify");
            }
            else
            {
                e = API_EARGS;
                break;
            }

            // concatenates query + validate pwd + confirm
            client->queryrecoverylink(code);
            break;
        }
        case MegaRequest::TYPE_PAUSE_TRANSFERS:
        {
            bool pause = request->getFlag();
            int direction = request->getNumber();
            if(direction != -1
                    && direction != MegaTransfer::TYPE_DOWNLOAD
                    && direction != MegaTransfer::TYPE_UPLOAD)
            {
                e = API_EARGS;
                break;
            }

            if(direction == -1)
            {
                client->pausexfers(PUT, pause);
                client->pausexfers(GET, pause);
            }
            else if(direction == MegaTransfer::TYPE_DOWNLOAD)
            {
                client->pausexfers(GET, pause);
            }
            else
            {
                client->pausexfers(PUT, pause);
            }

            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
        case MegaRequest::TYPE_CANCEL_TRANSFER:
        {
            int transferTag = request->getTransferTag();
            if(transferMap.find(transferTag) == transferMap.end()) { e = API_ENOENT; break; };

            MegaTransferPrivate* megaTransfer = transferMap.at(transferTag);

            if (!megaTransfer->isStreamingTransfer())
            {
                Transfer *transfer = megaTransfer->getTransfer();
                if (!transfer)
                {
                    e = API_ENOENT;
                    break;
                }

                #ifdef _WIN32
                    if(transfer->type==GET)
                    {
                        transfer->localfilename.append("", 1);
                        WIN32_FILE_ATTRIBUTE_DATA fad;
                        if (GetFileAttributesExW((LPCWSTR)transfer->localfilename.data(), GetFileExInfoStandard, &fad))
                            SetFileAttributesW((LPCWSTR)transfer->localfilename.data(), fad.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
                        transfer->localfilename.resize(transfer->localfilename.size()-1);
                    }
                #endif

                megaTransfer->setSyncTransfer(true);
                megaTransfer->setLastError(MegaError(API_EINCOMPLETE));

                file_list files = transfer->files;
                file_list::iterator iterator = files.begin();
                while (iterator != files.end())
                {
                    File *file = *iterator;
                    iterator++;
                    if(!file->syncxfer) client->stopxfer(file);
                }
                fireOnRequestFinish(request, MegaError(API_OK));
            }
            else
            {
                m_off_t startPos = megaTransfer->getStartPos();
                m_off_t endPos = megaTransfer->getEndPos();
                m_off_t totalBytes = endPos - startPos + 1;

                MegaNode *publicNode = megaTransfer->getPublicNode();
                if (publicNode)
                {
                    client->preadabort(publicNode->getHandle(), startPos, totalBytes);
                }
                else
                {
                    Node *node = client->nodebyhandle(megaTransfer->getNodeHandle());
                    if (node)
                    {
                        client->preadabort(node, startPos, totalBytes);
                    }
                }
                fireOnRequestFinish(request, MegaError(API_OK));
            }
            break;
        }
        case MegaRequest::TYPE_CANCEL_TRANSFERS:
        {
            int direction = request->getParamType();
            if((direction != MegaTransfer::TYPE_DOWNLOAD) && (direction != MegaTransfer::TYPE_UPLOAD))
                { e = API_EARGS; break; }

            for (transfer_map::iterator it = client->transfers[direction].begin() ; it != client->transfers[direction].end() ; )
            {
                Transfer *transfer = it->second;
                if(transferMap.find(transfer->tag) != transferMap.end())
                {
                    MegaTransferPrivate* megaTransfer = transferMap.at(transfer->tag);
                    megaTransfer->setSyncTransfer(true);
                    megaTransfer->setLastError(MegaError(API_EINCOMPLETE));
                }

                it++;

                file_list files = transfer->files;
				file_list::iterator iterator = files.begin();
				while (iterator != files.end())
				{
					File *file = *iterator;
					iterator++;
					if(!file->syncxfer) client->stopxfer(file);
				}
            }
            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
#ifdef ENABLE_SYNC
        case MegaRequest::TYPE_ADD_SYNC:
        {
            const char *localPath = request->getFile();
            Node *node = client->nodebyhandle(request->getNodeHandle());
            if(!node || (node->type==FILENODE) || !localPath)
            {
                e = API_EARGS;
                break;
            }

            string utf8name(localPath);
            string localname;
            client->fsaccess->path2local(&utf8name, &localname);
            e = client->addsync(&localname, DEBRISFOLDER, NULL, node, 0, -nextTag);
            if(!e)
            {
                MegaSyncPrivate *sync = new MegaSyncPrivate(client->syncs.back());
                sync->setListener(request->getSyncListener());
                syncMap[-nextTag] = sync;

                request->setNumber(client->syncs.back()->fsfp);
                fireOnRequestFinish(request, MegaError(API_OK));
            }
            break;
        }
        case MegaRequest::TYPE_REMOVE_SYNCS:
        {
            sync_list::iterator it = client->syncs.begin();
            while (it != client->syncs.end())
            {
                Sync *sync = (*it);
                int tag = sync->tag;
                it++;

                client->delsync(sync);

                if (syncMap.find(tag) != syncMap.end())
                {
                    MegaSyncPrivate *megaSync = syncMap.at(tag);
                    syncMap.erase(tag);
                    delete megaSync;
                }
            }
            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
        case MegaRequest::TYPE_REMOVE_SYNC:
        {
            handle nodehandle = request->getNodeHandle();
            sync_list::iterator it = client->syncs.begin();
            bool found = false;
            while(it != client->syncs.end())
            {
                Sync *sync = (*it);
                it++;

                int tag = sync->tag;
                if (!sync->localroot.node || sync->localroot.node->nodehandle == nodehandle)
                {
                    string path;
                    fsAccess->local2path(&sync->localroot.localname, &path);
                    if (!request->getFile() || sync->localroot.node)
                    {
                        request->setFile(path.c_str());
                    }

                    client->delsync(sync, request->getFlag());

                    if (syncMap.find(tag) != syncMap.end())
                    {
                        MegaSyncPrivate *megaSync = syncMap.at(tag);
                        syncMap.erase(tag);
                        delete megaSync;
                    }

                    found = true;
                }
            }

            if (found)
            {
                fireOnRequestFinish(request, MegaError(API_OK));
            }
            else
            {
                e = API_ENOENT;
            }

            break;
        }
#endif
        case MegaRequest::TYPE_REPORT_EVENT:
        {
            const char *details = request->getText();
            if(!details)
            {
                e = API_EARGS;
                break;
            }

            string event = "A"; //Application event
            int size = strlen(details);
            char *base64details = new char[size * 4 / 3 + 4];
            Base64::btoa((byte *)details, size, base64details);
            client->reportevent(event.c_str(), base64details);
            delete [] base64details;
            break;
        }
        case MegaRequest::TYPE_DELETE:
        {
#ifdef HAVE_LIBUV
            if (httpServer)
            {
                MegaHTTPServer *server = httpServer;
                httpServer = NULL;
                sdkMutex.unlock();
                delete server;
                sdkMutex.lock();
            }
#endif
            threadExit = 1;
            break;
        }
        case MegaRequest::TYPE_GET_PRICING:
        case MegaRequest::TYPE_GET_PAYMENT_ID:
        case MegaRequest::TYPE_UPGRADE_ACCOUNT:
        {
            int method = request->getNumber();
            if(method != MegaApi::PAYMENT_METHOD_BALANCE && method != MegaApi::PAYMENT_METHOD_CREDIT_CARD)
            {
                e = API_EARGS;
                break;
            }

            client->purchase_enumeratequotaitems();
            break;
        }
        case MegaRequest::TYPE_SUBMIT_PURCHASE_RECEIPT:
        {
            const char* receipt = request->getText();
            int type = request->getNumber();

            if(!receipt || (type != MegaApi::PAYMENT_METHOD_GOOGLE_WALLET
                            && type != MegaApi::PAYMENT_METHOD_ITUNES))
            {
                e = API_EARGS;
                break;
            }

            if(type == MegaApi::PAYMENT_METHOD_ITUNES && client->loggedin() != FULLACCOUNT)
            {
                e = API_EACCESS;
                break;
            }

            string base64receipt;
            if(type == MegaApi::PAYMENT_METHOD_GOOGLE_WALLET)
            {
                int len = strlen(receipt);
                base64receipt.resize(len * 4 / 3 + 4);
                base64receipt.resize(Base64::btoa((byte *)receipt, len, (char *)base64receipt.data()));
            }
            else // MegaApi::PAYMENT_METHOD_ITUNES
                 // MegaApi::PAYMENT_METHOD_WINDOWS_STORE
            {
                base64receipt = receipt;
            }

            client->submitpurchasereceipt(type, base64receipt.c_str());
            break;
        }
        case MegaRequest::TYPE_CREDIT_CARD_STORE:
        {
            const char *ccplain = request->getText();
            e = client->creditcardstore(ccplain);
            break;
        }
        case MegaRequest::TYPE_CREDIT_CARD_QUERY_SUBSCRIPTIONS:
        {
            client->creditcardquerysubscriptions();
            break;
        }
        case MegaRequest::TYPE_CREDIT_CARD_CANCEL_SUBSCRIPTIONS:
        {
            const char* reason = request->getText();
            client->creditcardcancelsubscriptions(reason);
            break;
        }
        case MegaRequest::TYPE_GET_PAYMENT_METHODS:
        {
            client->getpaymentmethods();
            break;
        }
        case MegaRequest::TYPE_SUBMIT_FEEDBACK:
        {
            int rating = request->getNumber();
            const char *message = request->getText();

            if(rating < 1 || rating > 5)
            {
                e = API_EARGS;
                break;
            }

            if(!message)
            {
                message = "";
            }

            int size = strlen(message);
            char *base64message = new char[size * 4 / 3 + 4];
            Base64::btoa((byte *)message, size, base64message);

            char base64uhandle[12];
            Base64::btoa((const byte*)&client->me, MegaClient::USERHANDLE, base64uhandle);

            string feedback;
            feedback.resize(128 + strlen(base64message));

            snprintf((char *)feedback.data(), feedback.size(), "{\\\"r\\\":\\\"%d\\\",\\\"m\\\":\\\"%s\\\",\\\"u\\\":\\\"%s\\\"}", rating, base64message, base64uhandle);
            client->userfeedbackstore(feedback.c_str());
            delete [] base64message;
            break;
        }
        case MegaRequest::TYPE_SEND_EVENT:
        {
            int number = request->getNumber();
            const char *text = request->getText();

            if(number < 99500 || number >= 99600 || !text)
            {
                e = API_EARGS;
                break;
            }

            client->sendevent(number, text);
            break;
        }
        case MegaRequest::TYPE_GET_USER_DATA:
        {
            const char *email = request->getEmail();
            if(request->getFlag() && !email)
            {
                e = API_EARGS;
                break;
            }

            if(!request->getFlag())
            {
                client->getuserdata();
            }
            else
            {
                client->getpubkey(email);
            }

            break;
        }
        case MegaRequest::TYPE_LOAD_BALANCING:
        {
            const char* service = request->getName();
            if(!service)
            {
                e = API_EARGS;
                break;
            }

            client->loadbalancing(service);
            break;
        }
        case MegaRequest::TYPE_KILL_SESSION:
        {
            MegaHandle handle = request->getNodeHandle();
            if (handle == INVALID_HANDLE)
            {
                client->killallsessions();
            }
            else
            {
                client->killsession(handle);
            }
            break;
        }
        case MegaRequest::TYPE_GET_SESSION_TRANSFER_URL:
        {
            client->copysession();
            break;
        }
        case MegaRequest::TYPE_CLEAN_RUBBISH_BIN:
        {
            client->cleanrubbishbin();
            break;
        }
        case MegaRequest::TYPE_USE_HTTPS_ONLY:
        {
            bool usehttps = request->getFlag();
            if (client->usehttps != usehttps)
            {
                client->usehttps = usehttps;
                for (int d = GET; d == GET || d == PUT; d += PUT - GET)
                {
                    for (transfer_map::iterator it = client->transfers[d].begin(); it != client->transfers[d].end(); it++)
                    {
                        Transfer *t = it->second;
                        if (t->slot)
                        {
                            t->failed(API_EAGAIN);
                        }
                    }
                }
            }
            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
        case MegaRequest::TYPE_SET_PROXY:
        {
            Proxy *proxy = request->getProxy();
            httpio->setproxy(proxy);
            delete proxy;
            fireOnRequestFinish(request, MegaError(API_OK));
            break;
        }
#ifdef ENABLE_CHAT
        case MegaRequest::TYPE_CHAT_CREATE:
        {
            MegaTextChatPeerList *chatPeers = request->getMegaTextChatPeerList();
            if (!chatPeers)   // refuse to create chats without participants
            {
                e = API_EARGS;
                break;
            }

            bool group = request->getFlag();
            const userpriv_vector *userpriv = ((MegaTextChatPeerListPrivate*)chatPeers)->getList();
            if (!userpriv || (!group && chatPeers->size() > 1))
            {
                e = API_EARGS;
                break;
            }

            client->createChat(group, userpriv);
            break;
        }
        case MegaRequest::TYPE_CHAT_FETCH:
        {
            client->fetchChats();
            break;
        }
        case MegaRequest::TYPE_CHAT_INVITE:
        {
            handle chatid = request->getNodeHandle();
            handle uh = request->getParentHandle();
            int access = request->getAccess();

            if (chatid == INVALID_HANDLE || uh == INVALID_HANDLE)
            {
                e = API_EARGS;
                break;
            }

            char uid[12];
            Base64::btoa((byte*)&uh, sizeof uh, uid);
            uid[11] = 0;

            client->inviteToChat(chatid, uid, access);
            break;
        }
        case MegaRequest::TYPE_CHAT_REMOVE:
        {
            handle chatid = request->getNodeHandle();
            handle uh = request->getParentHandle();

            if (chatid == INVALID_HANDLE)
            {
                e = API_EARGS;
                break;
            }

            // user is optional. If not provided, command apply to own user
            if (uh != INVALID_HANDLE)
            {
                char uid[12];
                Base64::btoa((byte*)&uh, sizeof uh, uid);
                uid[11] = 0;

                client->removeFromChat(chatid, uid);
            }
            else
            {
                client->removeFromChat(chatid);
            }
            break;
        }
        case MegaRequest::TYPE_CHAT_URL:
        {
            MegaHandle chatid = request->getNodeHandle();
            if (chatid == INVALID_HANDLE)
            {
                e = API_EARGS;
                break;
            }

            client->getUrlChat(chatid);
            break;
        }
        case MegaRequest::TYPE_CHAT_GRANT_ACCESS:
        {
            handle chatid = request->getParentHandle();
            handle h = request->getNodeHandle();
            const char *uid = request->getEmail();

            if (chatid == INVALID_HANDLE || h == INVALID_HANDLE || !uid)
            {
                e = API_EARGS;
                break;
            }

            client->grantAccessInChat(chatid, h, uid);
            break;
        }
        case MegaRequest::TYPE_CHAT_REMOVE_ACCESS:
        {
            handle chatid = request->getParentHandle();
            handle h = request->getNodeHandle();
            const char *uid = request->getEmail();

            if (chatid == INVALID_HANDLE || h == INVALID_HANDLE || !uid)
            {
                e = API_EARGS;
                break;
            }

            client->removeAccessInChat(chatid, h, uid);
            break;
        }
#endif
        default:
        {
            e = API_EINTERNAL;
        }
        }

		if(e)
        {
            LOG_err << "Error starting request: " << e;
            fireOnRequestFinish(request, MegaError(e));
        }

		sdkMutex.unlock();
	}
}

char* MegaApiImpl::stringToArray(string &buffer)
{
	char *newbuffer = new char[buffer.size()+1];
	memcpy(newbuffer, buffer.data(), buffer.size());
	newbuffer[buffer.size()]='\0';
    return newbuffer;
}

void MegaApiImpl::updateStats()
{
    sdkMutex.lock();
    pendingDownloads = client->transfers[0].size();
    pendingUploads = client->transfers[1].size();
    sdkMutex.unlock();
}

long long MegaApiImpl::getTotalDownloadedBytes()
{
    return totalDownloadedBytes;
}

long long MegaApiImpl::getTotalUploadedBytes()
{
    return totalUploadedBytes;
}

void MegaApiImpl::update()
{
#ifdef ENABLE_SYNC
    sdkMutex.lock();

    LOG_debug << "PendingCS? " << (client->pendingcs != NULL);
    if(client->curfa == client->newfa.end())
    {
        LOG_debug << "PendingFA? 0";
    }
    else
    {
        HttpReqCommandPutFA* fa = *client->curfa;
        if(fa)
        {
            LOG_debug << "PendingFA? " << client->newfa.size() << " STATUS: " << fa->status;
        }
    }

    LOG_debug << "FLAGS: " << client->syncactivity << " " << client->syncadded
              << " " << client->syncdownrequired << " " << client->syncdownretry
              << " " << client->syncfslockretry << " " << client->syncfsopsfailed
              << " " << client->syncnagleretry << " " << client->syncscanfailed
              << " " << client->syncops << " " << client->syncscanstate
              << " " << client->faputcompletion.size() << " " << client->synccreate.size()
              << " " << client->fetchingnodes << " " << client->pendingfa.size()
              << " " << client->xferpaused[0] << " " << client->xferpaused[1]
              << " " << client->transfers[0].size() << " " << client->transfers[1].size()
              << " " << client->syncscanstate << " " << client->statecurrent
              << " " << client->syncadding << " " << client->syncdebrisadding
              << " " << client->umindex.size() << " " << client->uhindex.size();

    sdkMutex.unlock();
#endif

    waiter->notify();
}

bool MegaApiImpl::isWaiting()
{
    return waiting || waitingRequest;
}

TreeProcCopy::TreeProcCopy()
{
	nn = NULL;
	nc = 0;
}

void TreeProcCopy::allocnodes()
{
	if(nc) nn = new NewNode[nc];
}

TreeProcCopy::~TreeProcCopy()
{
	//Will be deleted in putnodes_result
	//delete[] nn;
}

// determine node tree size (nn = NULL) or write node tree to new nodes array
void TreeProcCopy::proc(MegaClient* client, Node* n)
{
	if (nn)
	{
		string attrstring;
		SymmCipher key;
		NewNode* t = nn+--nc;

		// copy node
		t->source = NEW_NODE;
		t->type = n->type;
		t->nodehandle = n->nodehandle;
        t->parenthandle = n->parent ? n->parent->nodehandle : UNDEF;

		// copy key (if file) or generate new key (if folder)
		if (n->type == FILENODE) t->nodekey = n->nodekey;
		else
		{
			byte buf[FOLDERNODEKEYLENGTH];
			PrnGen::genblock(buf,sizeof buf);
			t->nodekey.assign((char*)buf,FOLDERNODEKEYLENGTH);
		}

		t->attrstring = new string;
		if(t->nodekey.size())
		{
			key.setkey((const byte*)t->nodekey.data(),n->type);

			n->attrs.getjson(&attrstring);
			client->makeattr(&key,t->attrstring,attrstring.c_str());
		}
	}
	else nc++;
}

TransferQueue::TransferQueue()
{
    mutex.init(false);
}

void TransferQueue::push(MegaTransferPrivate *transfer)
{
    mutex.lock();
    transfers.push_back(transfer);
    mutex.unlock();
}

void TransferQueue::push_front(MegaTransferPrivate *transfer)
{
    mutex.lock();
    transfers.push_front(transfer);
    mutex.unlock();
}

MegaTransferPrivate *TransferQueue::pop()
{
    mutex.lock();
    if(transfers.empty())
    {
        mutex.unlock();
        return NULL;
    }
    MegaTransferPrivate *transfer = transfers.front();
    transfers.pop_front();
    mutex.unlock();
    return transfer;
}

void TransferQueue::removeListener(MegaTransferListener *listener)
{
    mutex.lock();

    std::deque<MegaTransferPrivate *>::iterator it = transfers.begin();
    while(it != transfers.end())
    {
        MegaTransferPrivate *transfer = (*it);
        if(transfer->getListener() == listener)
            transfer->setListener(NULL);
        it++;
    }

    mutex.unlock();
}

RequestQueue::RequestQueue()
{
    mutex.init(false);
}

void RequestQueue::push(MegaRequestPrivate *request)
{
    mutex.lock();
    requests.push_back(request);
    mutex.unlock();
}

void RequestQueue::push_front(MegaRequestPrivate *request)
{
    mutex.lock();
    requests.push_front(request);
    mutex.unlock();
}

MegaRequestPrivate *RequestQueue::pop()
{
    mutex.lock();
    if(requests.empty())
    {
        mutex.unlock();
        return NULL;
    }
    MegaRequestPrivate *request = requests.front();
    requests.pop_front();
    mutex.unlock();
    return request;
}

void RequestQueue::removeListener(MegaRequestListener *listener)
{
    mutex.lock();

    std::deque<MegaRequestPrivate *>::iterator it = requests.begin();
    while(it != requests.end())
    {
        MegaRequestPrivate *request = (*it);
        if(request->getListener()==listener)
            request->setListener(NULL);
        it++;
    }

    mutex.unlock();
}

#ifdef ENABLE_SYNC
void RequestQueue::removeListener(MegaSyncListener *listener)
{
    mutex.lock();

    std::deque<MegaRequestPrivate *>::iterator it = requests.begin();
    while(it != requests.end())
    {
        MegaRequestPrivate *request = (*it);
        if(request->getSyncListener()==listener)
            request->setSyncListener(NULL);
        it++;
    }

    mutex.unlock();
}
#endif

MegaHashSignatureImpl::MegaHashSignatureImpl(const char *base64Key)
{
    hashSignature = new HashSignature(new Hash());
    asymmCypher = new AsymmCipher();

    string pubks;
    int len = strlen(base64Key)/4*3+3;
    pubks.resize(len);
    pubks.resize(Base64::atob(base64Key, (byte *)pubks.data(), len));
    asymmCypher->setkey(AsymmCipher::PUBKEY,(byte*)pubks.data(), pubks.size());
}

MegaHashSignatureImpl::~MegaHashSignatureImpl()
{
    delete hashSignature;
    delete asymmCypher;
}

void MegaHashSignatureImpl::init()
{
    hashSignature->get(asymmCypher, NULL, 0);
}

void MegaHashSignatureImpl::add(const char *data, unsigned size)
{
    hashSignature->add((const byte *)data, size);
}

bool MegaHashSignatureImpl::checkSignature(const char *base64Signature)
{
    char signature[512];
    int l = Base64::atob(base64Signature, (byte *)signature, sizeof(signature));
    if(l != sizeof(signature))
        return false;

    return hashSignature->check(asymmCypher, (const byte *)signature, sizeof(signature));
}

int MegaAccountDetailsPrivate::getProLevel()
{
    return details.pro_level;
}

int64_t MegaAccountDetailsPrivate::getProExpiration()
{
    return details.pro_until;
}

int MegaAccountDetailsPrivate::getSubscriptionStatus()
{
    if(details.subscription_type == 'S')
    {
        return MegaAccountDetails::SUBSCRIPTION_STATUS_VALID;
    }

    if(details.subscription_type == 'R')
    {
        return MegaAccountDetails::SUBSCRIPTION_STATUS_INVALID;
    }

    return MegaAccountDetails::SUBSCRIPTION_STATUS_NONE;
}

int64_t MegaAccountDetailsPrivate::getSubscriptionRenewTime()
{
    return details.subscription_renew;
}

char *MegaAccountDetailsPrivate::getSubscriptionMethod()
{
    return MegaApi::strdup(details.subscription_method.c_str());
}

char *MegaAccountDetailsPrivate::getSubscriptionCycle()
{
    return MegaApi::strdup(details.subscription_cycle);
}

long long MegaAccountDetailsPrivate::getStorageMax()
{
    return details.storage_max;
}

long long MegaAccountDetailsPrivate::getStorageUsed()
{
    return details.storage_used;
}

long long MegaAccountDetailsPrivate::getTransferMax()
{
    return details.transfer_max;
}

long long MegaAccountDetailsPrivate::getTransferOwnUsed()
{
    return details.transfer_own_used;
}

int MegaAccountDetailsPrivate::getNumUsageItems()
{
    return details.storage.size();
}

long long MegaAccountDetailsPrivate::getStorageUsed(MegaHandle handle)
{
    return details.storage[handle].bytes;
}

long long MegaAccountDetailsPrivate::getNumFiles(MegaHandle handle)
{
    return details.storage[handle].files;
}

long long MegaAccountDetailsPrivate::getNumFolders(MegaHandle handle)
{
    return details.storage[handle].folders;
}

MegaAccountDetails* MegaAccountDetailsPrivate::copy()
{
    return new MegaAccountDetailsPrivate(&details);
}

int MegaAccountDetailsPrivate::getNumBalances() const
{
    return details.balances.size();
}

MegaAccountBalance *MegaAccountDetailsPrivate::getBalance(int i) const
{
    if ((unsigned int)i < details.balances.size())
    {
        return MegaAccountBalancePrivate::fromAccountBalance(&(details.balances[(unsigned int)i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumSessions() const
{
    return details.sessions.size();
}

MegaAccountSession *MegaAccountDetailsPrivate::getSession(int i) const
{
    if ((unsigned int)i < details.sessions.size())
    {
        return MegaAccountSessionPrivate::fromAccountSession(&(details.sessions[(unsigned int)i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumPurchases() const
{
    return details.purchases.size();
}

MegaAccountPurchase *MegaAccountDetailsPrivate::getPurchase(int i) const
{
    if ((unsigned int)i < details.purchases.size())
    {
        return MegaAccountPurchasePrivate::fromAccountPurchase(&(details.purchases[(unsigned int)i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getNumTransactions() const
{
    return details.transactions.size();
}

MegaAccountTransaction *MegaAccountDetailsPrivate::getTransaction(int i) const
{
    if ((unsigned int)i < details.transactions.size())
    {
        return MegaAccountTransactionPrivate::fromAccountTransaction(&(details.transactions[(unsigned int)i]));
    }
    return NULL;
}

int MegaAccountDetailsPrivate::getTemporalBandwidthInterval()
{
    return details.transfer_hist.size();
}

long long MegaAccountDetailsPrivate::getTemporalBandwidth()
{
    long long result = 0;
    for (unsigned int i = 0; i < details.transfer_hist.size(); i++)
    {
        result += details.transfer_hist[i];
    }
    return result;
}

ExternalLogger::ExternalLogger()
{
	mutex.init(true);
	this->megaLogger = NULL;
	SimpleLogger::setOutputClass(this);

    //Initialize outputSettings map
    SimpleLogger::outputSettings[(LogLevel)logFatal];
    SimpleLogger::outputSettings[(LogLevel)logError];
    SimpleLogger::outputSettings[(LogLevel)logWarning];
    SimpleLogger::outputSettings[(LogLevel)logInfo];
    SimpleLogger::outputSettings[(LogLevel)logDebug];
    SimpleLogger::outputSettings[(LogLevel)logMax];
}

void ExternalLogger::setMegaLogger(MegaLogger *logger)
{
	this->megaLogger = logger;
}

void ExternalLogger::setLogLevel(int logLevel)
{
	SimpleLogger::setLogLevel((LogLevel)logLevel);
}

void ExternalLogger::postLog(int logLevel, const char *message, const char *filename, int line)
{
    if(SimpleLogger::logCurrentLevel < logLevel)
        return;

	if(!message)
	{
		message = "";
	}

	if(!filename)
	{
		filename = "";
	}

    mutex.lock();
	SimpleLogger((LogLevel)logLevel, filename, line) << message;
    mutex.unlock();
}

void ExternalLogger::log(const char *time, int loglevel, const char *source, const char *message)
{
	if(!time)
	{
		time = "";
	}

	if(!source)
	{
		source = "";
	}

	if(!message)
	{
		message = "";
	}

	mutex.lock();
	if(megaLogger)
	{
        megaLogger->log(time, loglevel, source, message);
	}
	else
	{
		cout << "[" << time << "][" << SimpleLogger::toStr((LogLevel)loglevel) << "] " << message << endl;
	}
	mutex.unlock();
}


OutShareProcessor::OutShareProcessor()
{

}

bool OutShareProcessor::processNode(Node *node)
{
    if(!node->outshares)
    {
        return true;
    }

    for (share_map::iterator it = node->outshares->begin(); it != node->outshares->end(); it++)
	{
        Share *share = it->second;
        if (share->user && (share->user->show == VISIBLE)) // public links have no user
        {
            shares.push_back(share);
            handles.push_back(node->nodehandle);
        }
	}

	return true;
}

vector<Share *> &OutShareProcessor::getShares()
{
	return shares;
}

vector<handle> &OutShareProcessor::getHandles()
{
	return handles;
}

PendingOutShareProcessor::PendingOutShareProcessor()
{

}

bool PendingOutShareProcessor::processNode(Node *node)
{
    if(!node->pendingshares)
    {
        return true;
    }

    for (share_map::iterator it = node->pendingshares->begin(); it != node->pendingshares->end(); it++)
    {
        shares.push_back(it->second);
        handles.push_back(node->nodehandle);
    }

    return true;
}

vector<Share *> &PendingOutShareProcessor::getShares()
{
    return shares;
}

vector<handle> &PendingOutShareProcessor::getHandles()
{
    return handles;
}

MegaPricingPrivate::~MegaPricingPrivate()
{
    for(unsigned i = 0; i < currency.size(); i++)
    {
        delete[] currency[i];
    }

    for(unsigned i = 0; i < description.size(); i++)
    {
        delete[] description[i];
    }

    for(unsigned i = 0; i < iosId.size(); i++)
    {
        delete[] iosId[i];
    }

    for(unsigned i = 0; i < androidId.size(); i++)
    {
        delete[] androidId[i];
    }
}

int MegaPricingPrivate::getNumProducts()
{
    return handles.size();
}

handle MegaPricingPrivate::getHandle(int productIndex)
{
    if((unsigned)productIndex < handles.size())
        return handles[productIndex];

    return UNDEF;
}

int MegaPricingPrivate::getProLevel(int productIndex)
{
    if((unsigned)productIndex < proLevel.size())
        return proLevel[productIndex];

    return 0;
}

int MegaPricingPrivate::getGBStorage(int productIndex)
{
    if((unsigned)productIndex < gbStorage.size())
        return gbStorage[productIndex];

    return 0;
}

int MegaPricingPrivate::getGBTransfer(int productIndex)
{
    if((unsigned)productIndex < gbTransfer.size())
        return gbTransfer[productIndex];

    return 0;
}

int MegaPricingPrivate::getMonths(int productIndex)
{
    if((unsigned)productIndex < months.size())
        return months[productIndex];

    return 0;
}

int MegaPricingPrivate::getAmount(int productIndex)
{
    if((unsigned)productIndex < amount.size())
        return amount[productIndex];

    return 0;
}

const char *MegaPricingPrivate::getCurrency(int productIndex)
{
    if((unsigned)productIndex < currency.size())
        return currency[productIndex];

    return NULL;
}

const char *MegaPricingPrivate::getDescription(int productIndex)
{
    if((unsigned)productIndex < description.size())
        return description[productIndex];

    return NULL;
}

const char *MegaPricingPrivate::getIosID(int productIndex)
{
    if((unsigned)productIndex < iosId.size())
        return iosId[productIndex];

    return NULL;
}

const char *MegaPricingPrivate::getAndroidID(int productIndex)
{
    if((unsigned)productIndex < androidId.size())
        return androidId[productIndex];

    return NULL;
}

MegaPricing *MegaPricingPrivate::copy()
{
    MegaPricingPrivate *megaPricing = new MegaPricingPrivate();
    for(unsigned i=0; i<handles.size(); i++)
    {
        megaPricing->addProduct(handles[i], proLevel[i], gbStorage[i], gbTransfer[i],
                                months[i], amount[i], currency[i], description[i], iosId[i], androidId[i]);
    }

    return megaPricing;
}

void MegaPricingPrivate::addProduct(handle product, int proLevel, int gbStorage, int gbTransfer, int months, int amount, const char *currency,
                                    const char* description, const char* iosid, const char* androidid)
{
    this->handles.push_back(product);
    this->proLevel.push_back(proLevel);
    this->gbStorage.push_back(gbStorage);
    this->gbTransfer.push_back(gbTransfer);
    this->months.push_back(months);
    this->amount.push_back(amount);
    this->currency.push_back(MegaApi::strdup(currency));
    this->description.push_back(MegaApi::strdup(description));
    this->iosId.push_back(MegaApi::strdup(iosid));
    this->androidId.push_back(MegaApi::strdup(androidid));
}

#ifdef ENABLE_SYNC
MegaSyncPrivate::MegaSyncPrivate(Sync *sync)
{
    this->tag = sync->tag;
    sync->client->fsaccess->local2path(&sync->localroot.localname, &localFolder);
    this->megaHandle = sync->localroot.node->nodehandle;
    this->fingerprint = sync->fsfp;
    this->state = sync->state;
    this->listener = NULL;
}

MegaSyncPrivate::MegaSyncPrivate(MegaSyncPrivate *sync)
{
    this->setTag(sync->getTag());
    this->setLocalFolder(sync->getLocalFolder());
    this->setMegaHandle(sync->getMegaHandle());
    this->setLocalFingerprint(sync->getLocalFingerprint());
    this->setState(sync->getState());
    this->setListener(sync->getListener());
}

MegaSyncPrivate::~MegaSyncPrivate()
{
}

MegaSync *MegaSyncPrivate::copy()
{
    return new MegaSyncPrivate(this);
}

MegaHandle MegaSyncPrivate::getMegaHandle() const
{
    return megaHandle;
}

void MegaSyncPrivate::setMegaHandle(MegaHandle handle)
{
    this->megaHandle = handle;
}

const char *MegaSyncPrivate::getLocalFolder() const
{
    if(!localFolder.size())
        return NULL;

    return localFolder.c_str();
}

void MegaSyncPrivate::setLocalFolder(const char *path)
{
    this->localFolder = path;
}

long long MegaSyncPrivate::getLocalFingerprint() const
{
    return fingerprint;
}

void MegaSyncPrivate::setLocalFingerprint(long long fingerprint)
{
    this->fingerprint = fingerprint;
}

int MegaSyncPrivate::getTag() const
{
    return tag;
}

void MegaSyncPrivate::setTag(int tag)
{
    this->tag = tag;
}

void MegaSyncPrivate::setListener(MegaSyncListener *listener)
{
    this->listener = listener;
}

MegaSyncListener *MegaSyncPrivate::getListener()
{
    return this->listener;
}

int MegaSyncPrivate::getState() const
{
    return state;
}

void MegaSyncPrivate::setState(int state)
{
    this->state = state;
}

MegaSyncEventPrivate::MegaSyncEventPrivate(int type)
{
    this->type = type;
    path = NULL;
    newPath = NULL;
    prevName = NULL;
    nodeHandle = INVALID_HANDLE;
    prevParent = INVALID_HANDLE;
}

MegaSyncEventPrivate::~MegaSyncEventPrivate()
{
    delete [] path;
}

MegaSyncEvent *MegaSyncEventPrivate::copy()
{
    MegaSyncEventPrivate *event = new MegaSyncEventPrivate(type);
    event->setPath(this->path);
    event->setNodeHandle(this->nodeHandle);
    event->setNewPath(this->newPath);
    event->setPrevName(this->prevName);
    event->setPrevParent(this->prevParent);
    return event;
}

int MegaSyncEventPrivate::getType() const
{
    return type;
}

const char *MegaSyncEventPrivate::getPath() const
{
    return path;
}

MegaHandle MegaSyncEventPrivate::getNodeHandle() const
{
    return nodeHandle;
}

const char *MegaSyncEventPrivate::getNewPath() const
{
    return newPath;
}

const char *MegaSyncEventPrivate::getPrevName() const
{
    return prevName;
}

MegaHandle MegaSyncEventPrivate::getPrevParent() const
{
    return prevParent;
}

void MegaSyncEventPrivate::setPath(const char *path)
{
    if(this->path)
    {
        delete [] this->path;
    }
    this->path =  MegaApi::strdup(path);
}

void MegaSyncEventPrivate::setNodeHandle(MegaHandle nodeHandle)
{
    this->nodeHandle = nodeHandle;
}

void MegaSyncEventPrivate::setNewPath(const char *newPath)
{
    if(this->newPath)
    {
        delete [] this->newPath;
    }
    this->newPath =  MegaApi::strdup(newPath);
}

void MegaSyncEventPrivate::setPrevName(const char *prevName)
{
    if(this->prevName)
    {
        delete [] this->prevName;
    }
    this->prevName =  MegaApi::strdup(prevName);
}

void MegaSyncEventPrivate::setPrevParent(MegaHandle prevParent)
{
    this->prevParent = prevParent;
}

#endif


MegaAccountBalance *MegaAccountBalancePrivate::fromAccountBalance(const AccountBalance *balance)
{
    return new MegaAccountBalancePrivate(balance);
}

MegaAccountBalancePrivate::~MegaAccountBalancePrivate()
{

}

MegaAccountBalance *MegaAccountBalancePrivate::copy()
{
    return new MegaAccountBalancePrivate(&balance);
}

double MegaAccountBalancePrivate::getAmount() const
{
    return balance.amount;
}

char *MegaAccountBalancePrivate::getCurrency() const
{
    return MegaApi::strdup(balance.currency);
}

MegaAccountBalancePrivate::MegaAccountBalancePrivate(const AccountBalance *balance)
{
    this->balance = *balance;
}

MegaAccountSession *MegaAccountSessionPrivate::fromAccountSession(const AccountSession *session)
{
    return new MegaAccountSessionPrivate(session);
}

MegaAccountSessionPrivate::~MegaAccountSessionPrivate()
{

}

MegaAccountSession *MegaAccountSessionPrivate::copy()
{
    return new MegaAccountSessionPrivate(&session);
}

int64_t MegaAccountSessionPrivate::getCreationTimestamp() const
{
    return session.timestamp;
}

int64_t MegaAccountSessionPrivate::getMostRecentUsage() const
{
    return session.mru;
}

char *MegaAccountSessionPrivate::getUserAgent() const
{
    return MegaApi::strdup(session.useragent.c_str());
}

char *MegaAccountSessionPrivate::getIP() const
{
    return MegaApi::strdup(session.ip.c_str());
}

char *MegaAccountSessionPrivate::getCountry() const
{
    return MegaApi::strdup(session.country);
}

bool MegaAccountSessionPrivate::isCurrent() const
{
    return session.current;
}

bool MegaAccountSessionPrivate::isAlive() const
{
    return session.alive;
}

MegaHandle MegaAccountSessionPrivate::getHandle() const
{
    return session.id;
}

MegaAccountSessionPrivate::MegaAccountSessionPrivate(const AccountSession *session)
{
    this->session = *session;
}


MegaAccountPurchase *MegaAccountPurchasePrivate::fromAccountPurchase(const AccountPurchase *purchase)
{
    return new MegaAccountPurchasePrivate(purchase);
}

MegaAccountPurchasePrivate::~MegaAccountPurchasePrivate()
{

}

MegaAccountPurchase *MegaAccountPurchasePrivate::copy()
{
    return new MegaAccountPurchasePrivate(&purchase);
}

int64_t MegaAccountPurchasePrivate::getTimestamp() const
{
    return purchase.timestamp;
}

char *MegaAccountPurchasePrivate::getHandle() const
{
    return MegaApi::strdup(purchase.handle);
}

char *MegaAccountPurchasePrivate::getCurrency() const
{
    return MegaApi::strdup(purchase.currency);
}

double MegaAccountPurchasePrivate::getAmount() const
{
    return purchase.amount;
}

int MegaAccountPurchasePrivate::getMethod() const
{
    return purchase.method;
}

MegaAccountPurchasePrivate::MegaAccountPurchasePrivate(const AccountPurchase *purchase)
{
    this->purchase = *purchase;
}


MegaAccountTransaction *MegaAccountTransactionPrivate::fromAccountTransaction(const AccountTransaction *transaction)
{
    return new MegaAccountTransactionPrivate(transaction);
}

MegaAccountTransactionPrivate::~MegaAccountTransactionPrivate()
{

}

MegaAccountTransaction *MegaAccountTransactionPrivate::copy()
{
    return new MegaAccountTransactionPrivate(&transaction);
}

int64_t MegaAccountTransactionPrivate::getTimestamp() const
{
    return transaction.timestamp;
}

char *MegaAccountTransactionPrivate::getHandle() const
{
    return MegaApi::strdup(transaction.handle);
}

char *MegaAccountTransactionPrivate::getCurrency() const
{
    return MegaApi::strdup(transaction.currency);
}

double MegaAccountTransactionPrivate::getAmount() const
{
    return transaction.delta;
}

MegaAccountTransactionPrivate::MegaAccountTransactionPrivate(const AccountTransaction *transaction)
{
    this->transaction = *transaction;
}



ExternalInputStream::ExternalInputStream(MegaInputStream *inputStream)
{
    this->inputStream = inputStream;
}

m_off_t ExternalInputStream::size()
{
    return inputStream->getSize();
}

bool ExternalInputStream::read(byte *buffer, unsigned size)
{
    return inputStream->read((char *)buffer, size);
}


FileInputStream::FileInputStream(FileAccess *fileAccess)
{
    this->fileAccess = fileAccess;
    this->offset = 0;
}

m_off_t FileInputStream::size()
{
    return fileAccess->size;
}

bool FileInputStream::read(byte *buffer, unsigned size)
{
    if (!buffer)
    {
        if ((offset + size) <= fileAccess->size)
        {
            offset += size;
            return true;
        }

        LOG_warn << "Invalid seek on FileInputStream";
        return false;
    }

    if (fileAccess->sysread(buffer, size, offset))
    {
        offset += size;
        return true;
    }

    LOG_warn << "Invalid read on FileInputStream";
    return false;
}

FileInputStream::~FileInputStream()
{

}


MegaFolderUploadController::MegaFolderUploadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer)
{
    this->megaApi = megaApi;
    this->client = megaApi->getMegaClient();
    this->transfer = transfer;
    this->listener = transfer->getListener();
    this->recursive = 0;
    this->pendingTransfers = 0;
    this->tag = transfer->getTag();
}

void MegaFolderUploadController::start()
{
    transfer->setFolderTransferTag(-1);
    transfer->setStartTime(Waiter::ds);
    megaApi->fireOnTransferStart(transfer);

    const char *name = transfer->getFileName();
    MegaNode *parent = megaApi->getNodeByHandle(transfer->getParentHandle());
    if(!parent)
    {
        megaApi->fireOnTransferFinish(transfer, MegaError(API_EARGS));
        delete this;
    }
    else
    {
        string path = transfer->getPath();
        string localpath;
        client->fsaccess->path2local(&path, &localpath);

        MegaNode *child = megaApi->getChildNode(parent, name);

        if(!child || !child->isFolder())
        {
            pendingFolders.push_back(localpath);
            megaApi->createFolder(name, parent, this);
        }
        else
        {
            pendingFolders.push_front(localpath);
            onFolderAvailable(child->getHandle());
        }

        delete child;
        delete parent;
    }
}

void MegaFolderUploadController::onFolderAvailable(MegaHandle handle)
{
    recursive++;
    string localPath = pendingFolders.front();
    pendingFolders.pop_front();

    MegaNode *parent = megaApi->getNodeByHandle(handle);

    string localname;
    DirAccess* da;
    da = client->fsaccess->newdiraccess();
    if (da->dopen(&localPath, NULL, false))
    {
        size_t t = localPath.size();

        while (da->dnext(&localPath, &localname, client->followsymlinks))
        {
            if (t)
            {
                localPath.append(client->fsaccess->localseparator);
            }

            localPath.append(localname);

            FileAccess *fa = client->fsaccess->newfileaccess();
            if(fa->fopen(&localPath, true, false))
            {
                string name = localname;
                client->fsaccess->local2name(&name);

                if(fa->type == FILENODE)
                {
                    pendingTransfers++;
                    MegaNode *child = megaApi->getChildNode(parent, name.c_str());
                    if(!child || child->isFolder() || (fa->size != child->getSize()))
                    {                        
                        FileFingerprint fp;
                        fp.genfingerprint(fa);
                        Node *node = client->nodebyfingerprint(&fp);
                        if(!node)
                        {
                            string utf8path;
                            client->fsaccess->local2path(&localPath, &utf8path);
                            megaApi->startUpload(utf8path.c_str(), parent, (const char *)NULL, -1, tag, this);
                        }
                        else
                        {
                            string utf8path;
                            client->fsaccess->local2path(&localPath, &utf8path);
                            #if defined(_WIN32) && !defined(WINDOWS_PHONE)
                                    if(!PathIsRelativeA(utf8path.c_str()) && ((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\")))
                                        utf8path.insert(0, "\\\\?\\");
                            #endif

                            int nextTag = client->nextreqtag();
                            MegaTransferPrivate* t = new MegaTransferPrivate(MegaTransfer::TYPE_UPLOAD, this);
                            t->setPath(utf8path.c_str());
                            t->setParentHandle(parent->getHandle());
                            t->setTag(nextTag);
                            t->setFolderTransferTag(tag);
                            t->setTotalBytes(node->size);
                            megaApi->transferMap[nextTag] = t;
                            pendingSkippedTransfers.push_back(t);
                            megaApi->fireOnTransferStart(t);

                            MegaNode *duplicate = MegaNodePrivate::fromNode(node);
                            megaApi->copyNode(duplicate, parent, name.c_str(), this);
                            delete duplicate;
                        }
                    }
                    else
                    {
                        string utf8path;
                        client->fsaccess->local2path(&localPath, &utf8path);
                        #if defined(_WIN32) && !defined(WINDOWS_PHONE)
                                if(!PathIsRelativeA(utf8path.c_str()) && ((utf8path.size()<2) || utf8path.compare(0, 2, "\\\\")))
                                    utf8path.insert(0, "\\\\?\\");
                        #endif

                        int nextTag = client->nextreqtag();
                        MegaTransferPrivate* t = new MegaTransferPrivate(MegaTransfer::TYPE_UPLOAD, this);
                        t->setPath(utf8path.data());
                        t->setParentHandle(parent->getHandle());
                        t->setTag(nextTag);
                        t->setFolderTransferTag(tag);
                        t->setTotalBytes(child->getSize());
                        megaApi->transferMap[nextTag] = t;
                        megaApi->fireOnTransferStart(t);

                        t->setTransferredBytes(child->getSize());
                        t->setDeltaSize(child->getSize());
                        megaApi->fireOnTransferFinish(t, MegaError(API_OK));
                    }

                    delete child;
                }
                else
                {
                    MegaNode *child = megaApi->getChildNode(parent, name.c_str());
                    if(!child || !child->isFolder())
                    {
                        pendingFolders.push_back(localPath);
                        megaApi->createFolder(name.c_str(), parent, this);
                    }
                    else
                    {
                        pendingFolders.push_front(localPath);
                        onFolderAvailable(child->getHandle());
                    }
                    delete child;
                }
            }

            localPath.resize(t);
            delete fa;
        }
    }

    delete da;
    delete parent;
    recursive--;

    checkCompletion();
}

void MegaFolderUploadController::checkCompletion()
{
    if(!recursive && !pendingFolders.size() && !pendingTransfers && !pendingSkippedTransfers.size())
    {
        LOG_debug << "Folder transfer finished - " << transfer->getTransferredBytes() << " of " << transfer->getTotalBytes();
        megaApi->fireOnTransferFinish(transfer, MegaError(API_OK));
        delete this;
    }
}

void MegaFolderUploadController::onRequestFinish(MegaApi *, MegaRequest *request, MegaError *e)
{
    int type = request->getType();
    int errorCode = e->getErrorCode();

    if(type == MegaRequest::TYPE_CREATE_FOLDER)
    {
        if(!errorCode)
        {
            onFolderAvailable(request->getNodeHandle());
        }
        else
        {
            pendingFolders.pop_front();
            checkCompletion();
        }
    }
    else if(type == MegaRequest::TYPE_COPY)
    {
        Node *node = client->nodebyhandle(request->getNodeHandle());

        MegaTransferPrivate *t = pendingSkippedTransfers.front();
        t->setTransferredBytes(node->size);
        t->setDeltaSize(node->size);
        megaApi->fireOnTransferFinish(t, MegaError(API_OK));
        pendingSkippedTransfers.pop_front();
        checkCompletion();
    }
}

void MegaFolderUploadController::onTransferStart(MegaApi *, MegaTransfer *t)
{
    transfer->setTotalBytes(transfer->getTotalBytes() + t->getTotalBytes());
    transfer->setUpdateTime(Waiter::ds);
    megaApi->fireOnTransferUpdate(transfer);
}

void MegaFolderUploadController::onTransferUpdate(MegaApi *, MegaTransfer *t)
{
    transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
    transfer->setUpdateTime(Waiter::ds);
    transfer->setSpeed(t->getSpeed());
    megaApi->fireOnTransferUpdate(transfer);
}

void MegaFolderUploadController::onTransferFinish(MegaApi *, MegaTransfer *t, MegaError *)
{
    pendingTransfers--;
    transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
    transfer->setUpdateTime(Waiter::ds);

    if(t->getSpeed())
    {
        transfer->setSpeed(t->getSpeed());
    }

    megaApi->fireOnTransferUpdate(transfer);
    checkCompletion();
}

MegaFolderDownloadController::MegaFolderDownloadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer)
{
    this->megaApi = megaApi;
    this->client = megaApi->getMegaClient();
    this->transfer = transfer;
    this->listener = transfer->getListener();
    this->recursive = 0;
    this->pendingTransfers = 0;
    this->tag = transfer->getTag();
}

void MegaFolderDownloadController::start()
{
    transfer->setFolderTransferTag(-1);
    transfer->setStartTime(Waiter::ds);
    megaApi->fireOnTransferStart(transfer);

    const char *parentPath = transfer->getParentPath();
    const char *fileName = transfer->getFileName();
    Node *node = client->nodebyhandle(transfer->getNodeHandle());

    string name;
    string securename;
    string path;

    if (parentPath)
    {
        path = parentPath;
    }
    else
    {
        string separator;
        client->fsaccess->local2path(&client->fsaccess->localseparator, &separator);
        path = ".";
        path.append(separator);
    }

    if (!fileName)
    {
        attr_map::iterator ait = node->attrs.map.find('n');
        if (ait == node->attrs.map.end())
        {
            name = "CRYPTO_ERROR";
        }
        else if (!ait->second.size())
        {
            name = "BLANK";
        }
        else
        {
            name = ait->second;
        }
    }
    else
    {
        name = fileName;
    }

    client->fsaccess->name2local(&name);
    client->fsaccess->local2path(&name, &securename);
    path += securename;

#if defined(_WIN32) && !defined(WINDOWS_PHONE)
    if (!PathIsRelativeA(path.c_str()) && ((path.size()<2) || path.compare(0, 2, "\\\\")))
        path.insert(0, "\\\\?\\");
#endif

    transfer->setPath(path.c_str());
    downloadFolderNode(node, &path);
}

void MegaFolderDownloadController::downloadFolderNode(Node *node, string *path)
{
    recursive++;

    string localpath;
    client->fsaccess->path2local(path, &localpath);
    FileAccess *da = client->fsaccess->newfileaccess();
    if (!da->fopen(&localpath, true, false))
    {
        client->fsaccess->mkdirlocal(&localpath);
    }
    else if (da->type != FILENODE)
    {
        LOG_debug << "Already existing folder detected: " << *path;
    }
    else
    {
        delete da;
        LOG_err << "Local file detected where there should be a folder: " << *path;

        recursive--;
        checkCompletion();

        return;
    }
    delete da;

    localpath.append(client->fsaccess->localseparator);
    for (node_list::iterator it = node->children.begin(); it != node->children.end(); it++)
    {
        Node *child = (*it);
        int l = localpath.size();

        string name;
        attr_map::iterator ait = child->attrs.map.find('n');
        if (ait == child->attrs.map.end())
        {
            name = "CRYPTO_ERROR";
        }
        else if (!ait->second.size())
        {
            name = "BLANK";
        }
        else
        {
            name = ait->second;
        }

        client->fsaccess->name2local(&name);
        localpath.append(name);

        string utf8path;
        client->fsaccess->local2path(&localpath, &utf8path);

        if (child->type == FILENODE)
        {
            pendingTransfers++;
            FileAccess *fa = client->fsaccess->newfileaccess();
            if (fa->fopen(&localpath, true, false) && fa->type == FILENODE)
            {
                FileFingerprint fp;
                fp.genfingerprint(fa);
                if ((fp.isvalid && child->isvalid && fp == *(FileFingerprint *)child)
                        || (!child->isvalid && fa->size == child->size && fa->mtime == child->mtime))
                {
                    LOG_debug << "Already downloaded file detected: " << utf8path;
                    int nextTag = client->nextreqtag();
                    MegaTransferPrivate* t = new MegaTransferPrivate(MegaTransfer::TYPE_DOWNLOAD, this);

                    t->setPath(utf8path.data());
                    t->setNodeHandle(child->nodehandle);

                    t->setTag(nextTag);
                    t->setFolderTransferTag(tag);
                    t->setTotalBytes(child->size);
                    megaApi->transferMap[nextTag] = t;
                    megaApi->fireOnTransferStart(t);

                    t->setTransferredBytes(child->size);
                    t->setDeltaSize(child->size);
                    megaApi->fireOnTransferFinish(t, MegaError(API_OK));
                    localpath.resize(l);
                    delete fa;
                    continue;
                }
            }
            delete fa;

            MegaNode *megaChild = MegaNodePrivate::fromNode(child);
            megaApi->startDownload(megaChild, utf8path.c_str(), 0, 0, tag, NULL, this);
            delete megaChild;
        }
        else
        {
            downloadFolderNode(child, &utf8path);
        }

        localpath.resize(l);
    }

    recursive--;
    checkCompletion();
}

void MegaFolderDownloadController::checkCompletion()
{
    if (!recursive && !pendingTransfers)
    {
        LOG_debug << "Folder download finished - " << transfer->getTransferredBytes() << " of " << transfer->getTotalBytes();
        megaApi->fireOnTransferFinish(transfer, MegaError(API_OK));
        delete this;
    }
}

void MegaFolderDownloadController::onTransferStart(MegaApi *, MegaTransfer *t)
{
    transfer->setTotalBytes(transfer->getTotalBytes() + t->getTotalBytes());
    transfer->setUpdateTime(Waiter::ds);
    megaApi->fireOnTransferUpdate(transfer);
}

void MegaFolderDownloadController::onTransferUpdate(MegaApi *, MegaTransfer *t)
{
    transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
    transfer->setUpdateTime(Waiter::ds);
    transfer->setSpeed(t->getSpeed());
    megaApi->fireOnTransferUpdate(transfer);
}

void MegaFolderDownloadController::onTransferFinish(MegaApi *, MegaTransfer *t, MegaError *e)
{
    pendingTransfers--;
    transfer->setTransferredBytes(transfer->getTransferredBytes() + t->getDeltaSize());
    transfer->setUpdateTime(Waiter::ds);

    if (t->getSpeed())
    {
        transfer->setSpeed(t->getSpeed());
    }

    megaApi->fireOnTransferUpdate(transfer);
    checkCompletion();
}

#ifdef HAVE_LIBUV
StreamingBuffer::StreamingBuffer()
{
    this->capacity = 0;
    this->buffer = NULL;
    this->inpos = 0;
    this->outpos = 0;
    this->size = 0;
    this->free = 0;
    this->maxBufferSize = MAX_BUFFER_SIZE;
    this->maxOutputSize = MAX_OUTPUT_SIZE;
}

StreamingBuffer::~StreamingBuffer()
{
    delete [] buffer;
}

void StreamingBuffer::init(unsigned int capacity)
{
    if (capacity > maxBufferSize)
    {
        capacity = maxBufferSize;
    }

    this->capacity = capacity;
    this->buffer = new char[capacity];
    this->inpos = 0;
    this->outpos = 0;
    this->size = 0;
    this->free = capacity;
}

unsigned int StreamingBuffer::append(const char *buf, unsigned int len)
{
    if (!buffer)
    {
        // initialize the buffer if it's not initialized yet
        init(len);
    }

    if (free < len)
    {
        LOG_debug << "Not enough available space";
        len = free;
    }

    // update the internal state
    int currentIndex = inpos;
    inpos += len;
    int remaining = inpos - capacity;
    inpos %= capacity;
    size += len;
    free -= len;

    // append the new data
    if (remaining <= 0)
    {
        memcpy(buffer + currentIndex, buf, len);
    }
    else
    {
        int num = len - remaining;
        memcpy(buffer + currentIndex, buf, num);
        memcpy(buffer, buf + num, remaining);
    }

    return len;
}

unsigned int StreamingBuffer::availableData()
{
    return size;
}

unsigned int StreamingBuffer::availableSpace()
{
    return free;
}

unsigned int StreamingBuffer::availableCapacity()
{
    return capacity;
}

uv_buf_t StreamingBuffer::nextBuffer()
{
    if (!size)
    {
        // no data available
        return uv_buf_init(NULL, 0);
    }

    // prepare output buffer
    char *outbuf = buffer + outpos;
    int len = size < maxOutputSize ? size : maxOutputSize;
    if (outpos + len > capacity)
    {
        len = capacity - outpos;
    }

    // update the internal state
    size -= len;
    outpos += len;
    outpos %= capacity;

    // return the buffer
    return uv_buf_init(outbuf, len);
}

void StreamingBuffer::freeData(unsigned int len)
{
    // update the internal state
    free += len;
}

void StreamingBuffer::setMaxBufferSize(unsigned int bufferSize)
{
    if (bufferSize)
    {
        this->maxBufferSize = bufferSize;
    }
    else
    {
        this->maxBufferSize = MAX_BUFFER_SIZE;
    }
}

void StreamingBuffer::setMaxOutputSize(unsigned int outputSize)
{
    if (outputSize)
    {
        this->maxOutputSize = outputSize;
    }
    else
    {
        this->maxOutputSize = MAX_OUTPUT_SIZE;
    }
}

// http_parser settings
http_parser_settings MegaHTTPServer::parsercfg;

MegaHTTPServer::MegaHTTPServer(MegaApiImpl *megaApi)
{
    this->megaApi = megaApi;
    this->localOnly = true;
    this->started = false;
    this->port = 0;
    this->maxBufferSize = 0;
    this->maxOutputSize = 0;
    this->fileServerEnabled = true;
    this->folderServerEnabled = true;
    this->restrictedMode = MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS;
    this->lastHandle = INVALID_HANDLE;
    this->subtitlesSupportEnabled = false;
}

MegaHTTPServer::~MegaHTTPServer()
{
    stop();
}

bool MegaHTTPServer::start(int port, bool localOnly)
{
    if (started && this->port == port && this->localOnly == localOnly)
    {
        return true;
    }
    stop();

    this->port = port;
    this->localOnly = localOnly;
    uv_sem_init(&semaphore, 0);
    thread.start(threadEntryPoint, this);
    uv_sem_wait(&semaphore);
    uv_sem_destroy(&semaphore);
    return started;
}

void MegaHTTPServer::run()
{
    // parser callbacks
    parsercfg.on_url = onUrlReceived;
    parsercfg.on_message_begin = onMessageBegin;
    parsercfg.on_headers_complete = onHeadersComplete;
    parsercfg.on_message_complete = onMessageComplete;
    parsercfg.on_header_field = onHeaderField;
    parsercfg.on_header_value = onHeaderValue;
    parsercfg.on_body = onBody;

    uv_loop_t *uv_loop = uv_default_loop();

    uv_async_init(uv_loop, &exit_handle, onCloseRequested);
    exit_handle.data = this;

    uv_tcp_init(uv_loop, &server);
    server.data = this;

    uv_tcp_keepalive(&server, 0, 0);

    struct sockaddr_in address;
    if (localOnly)
    {
        uv_ip4_addr("127.0.0.1", port, &address);
    }
    else
    {
        uv_ip4_addr("0.0.0.0", port, &address);
    }

    if(uv_tcp_bind(&server, (const struct sockaddr*)&address, 0)
        || uv_listen((uv_stream_t*)&server, 32, onNewClient))
    {
        port = 0;
        uv_sem_post(&semaphore);
        return;
    }

    LOG_info << "HTTP server started on port " << port;
    started = true;
    uv_sem_post(&semaphore);
    uv_run(uv_loop, UV_RUN_DEFAULT);

    uv_loop_close(uv_loop);
    started = false;
    port = 0;

    LOG_debug << "HTTP server thread exit";
}

void MegaHTTPServer::stop()
{
    if (!started)
    {
        return;
    }

    uv_async_send(&exit_handle);
    thread.join();
}

int MegaHTTPServer::getPort()
{
    return port;
}

bool MegaHTTPServer::isLocalOnly()
{
    return localOnly;
}

void MegaHTTPServer::setMaxBufferSize(int bufferSize)
{
    this->maxBufferSize = bufferSize <= 0 ? 0 : bufferSize;
}

void MegaHTTPServer::setMaxOutputSize(int outputSize)
{
    this->maxOutputSize = outputSize <= 0 ? 0 : outputSize;
}

int MegaHTTPServer::getMaxBufferSize()
{
    if (maxBufferSize)
    {
        return maxBufferSize;
    }

    return StreamingBuffer::MAX_BUFFER_SIZE;
}

int MegaHTTPServer::getMaxOutputSize()
{
    if (maxOutputSize)
    {
        return maxOutputSize;
    }

    return StreamingBuffer::MAX_OUTPUT_SIZE;
}

void MegaHTTPServer::enableFileServer(bool enable)
{
    this->fileServerEnabled = enable;
}

void MegaHTTPServer::enableFolderServer(bool enable)
{
    this->folderServerEnabled = enable;
}

void MegaHTTPServer::setRestrictedMode(int mode)
{
    this->restrictedMode = mode;
}

bool MegaHTTPServer::isFileServerEnabled()
{
    return fileServerEnabled;
}

bool MegaHTTPServer::isFolderServerEnabled()
{
    return folderServerEnabled;
}

int MegaHTTPServer::getRestrictedMode()
{
    return restrictedMode;
}

bool MegaHTTPServer::isHandleAllowed(handle h)
{
    return restrictedMode == MegaApi::HTTP_SERVER_ALLOW_ALL
            || (restrictedMode == MegaApi::HTTP_SERVER_ALLOW_CREATED_LOCAL_LINKS && allowedHandles.count(h))
            || (restrictedMode == MegaApi::HTTP_SERVER_ALLOW_LAST_LOCAL_LINK && h == lastHandle);
}

void MegaHTTPServer::clearAllowedHandles()
{
    allowedHandles.clear();
    lastHandle = INVALID_HANDLE;
}

char *MegaHTTPServer::getLink(MegaNode *node)
{
    if (!node)
    {
        return NULL;
    }

    lastHandle = node->getHandle();
    allowedHandles.insert(lastHandle);

    ostringstream oss;
    oss << "http://127.0.0.1:" << port << "/";
    char *base64handle = node->getBase64Handle();
    oss << base64handle;
    delete [] base64handle;

    if (node->isPublic())
    {
        char *base64key = node->getBase64Key();
        oss << "!" << base64key;
        delete [] base64key;
    }

    oss << "/";

    string name = node->getName();
    string escapedName;
    URLCodec::escape(&name, &escapedName);
    oss << escapedName;
    string link = oss.str();
    return MegaApi::strdup(link.c_str());
}

bool MegaHTTPServer::isSubtitlesSupportEnabled()
{
    return subtitlesSupportEnabled;
}

void MegaHTTPServer::enableSubtitlesSupport(bool enable)
{
    this->subtitlesSupportEnabled = enable;
}

void *MegaHTTPServer::threadEntryPoint(void *param)
{
#ifndef _WIN32
    struct sigaction noaction;
    memset(&noaction, 0, sizeof(noaction));
    noaction.sa_handler = SIG_IGN;
    ::sigaction(SIGPIPE, &noaction, 0);
#endif

    MegaHTTPServer *httpServer = (MegaHTTPServer *)param;
    httpServer->run();
    return NULL;
}

void MegaHTTPServer::onNewClient(uv_stream_t* server_handle, int status)
{
    if (status < 0)
    {
        return;
    }

    // Create an object to save context information
    MegaHTTPContext* httpctx = new MegaHTTPContext();

    // Initialize the parser
    http_parser_init(&httpctx->parser, HTTP_REQUEST);

    // Set connection data
    httpctx->server = (MegaHTTPServer *)(server_handle->data);
    httpctx->megaApi = httpctx->server->megaApi;
    httpctx->parser.data = httpctx;
    httpctx->tcphandle.data = httpctx;
    httpctx->asynchandle.data = httpctx;
    httpctx->server->connections.push_back(httpctx);
    LOG_debug << "Connection received! " << httpctx->server->connections.size();

    // Async handle to perform writes
    uv_async_init(uv_default_loop(), &httpctx->asynchandle, onAsyncEvent);

    // Accept the connection
    uv_tcp_init(uv_default_loop(), &httpctx->tcphandle);
    uv_accept(server_handle, (uv_stream_t*)&httpctx->tcphandle);

    // Start reading
    uv_read_start((uv_stream_t*)&httpctx->tcphandle, allocBuffer, onDataReceived);
}

void MegaHTTPServer::allocBuffer(uv_handle_t *, size_t suggested_size, uv_buf_t* buf)
{
    // Reserve a buffer with the suggested size
    *buf = uv_buf_init(new char[suggested_size], suggested_size);
}

void MegaHTTPServer::onDataReceived(uv_stream_t* tcp, ssize_t nread, const uv_buf_t * buf)
{
    ssize_t parsed = -1;
    MegaHTTPContext *httpctx = (MegaHTTPContext*) tcp->data;
    if (nread >= 0)
    {
        parsed = http_parser_execute(&httpctx->parser, &parsercfg, buf->base, nread);
    }
    delete [] buf->base;

    if (parsed < 0 || nread < 0 || parsed < nread || httpctx->parser.upgrade)
    {
        httpctx->finished = true;
        LOG_debug << "Finishing request. Connection reset by peer or unsupported data";
        if (!uv_is_closing((uv_handle_t*)&httpctx->tcphandle))
        {
            uv_close((uv_handle_t*)&httpctx->tcphandle, onClose);
        }
    }
}

void MegaHTTPServer::onClose(uv_handle_t* handle)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) handle->data;

    // streaming transfers are automatically stopped when their listener is removed
    httpctx->megaApi->removeTransferListener(httpctx);
    httpctx->megaApi->removeRequestListener(httpctx);

    httpctx->server->connections.remove(httpctx);
    LOG_debug << "Connection closed: " << httpctx->server->connections.size();

    uv_close((uv_handle_t *)&httpctx->asynchandle, onAsyncEventClose);
}

void MegaHTTPServer::onAsyncEventClose(uv_handle_t *handle)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) handle->data;

    if (httpctx->resultCode == API_EINTERNAL)
    {
        httpctx->resultCode = API_EINCOMPLETE;
    }

    if (httpctx->transfer)
    {
        httpctx->megaApi->cancelTransfer(httpctx->transfer);
        httpctx->megaApi->fireOnStreamingFinish(httpctx->transfer, MegaError(httpctx->resultCode));
    }

    delete httpctx->node;
    delete httpctx;
    LOG_debug << "Connection deleted";
}

int MegaHTTPServer::onMessageBegin(http_parser *)
{
    return 0;
}

int MegaHTTPServer::onHeadersComplete(http_parser *)
{
    return 0;
}

int MegaHTTPServer::onUrlReceived(http_parser *parser, const char *url, size_t length)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;
    httpctx->path.assign(url, length);
    LOG_debug << "URL received: " << httpctx->path;

    if (length < 9 || url[0] != '/' || (length >= 10 && url[9] != '/' && url[9] != '!'))
    {
        LOG_debug << "URL without node handle";
        return 0;
    }

    unsigned int index = 9;
    httpctx->nodehandle.assign(url + 1, 8);
    LOG_debug << "Node handle: " << httpctx->nodehandle;

    if (length > 53 && url[index] == '!')
    {
        httpctx->nodekey.assign(url + 10, 43);
        LOG_debug << "Public link. Key: " << httpctx->nodekey;
        index = 53;
    }

    if (length > index && url[index] != '/')
    {
        LOG_warn << "Invalid URL";
        return 0;
    }

    index++;
    if (length > index)
    {
        string nodename(url + index, length - index);
        URLCodec::unescape(&nodename, &httpctx->nodename);
        LOG_debug << "Node name: " << httpctx->nodename;
    }

    return 0;
}

int MegaHTTPServer::onHeaderField(http_parser *parser, const char *at, size_t length)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;

    if (length == 5 && !memcmp(at, "Range", 5))
    {
        httpctx->range = true;
        LOG_debug << "Range header detected";
    }
    return 0;
}

int MegaHTTPServer::onHeaderValue(http_parser *parser, const char *at, size_t length)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;
    string value(at, length);
    size_t index;
    char *endptr;

    if (httpctx->range)
    {
        LOG_debug << "Range header value: " << value;
        httpctx->range = false;
        if (length > 7 && !memcmp(at, "bytes=", 6)
                && ((index = value.find_first_of('-')) != string::npos))
        {
            endptr = (char *)value.c_str();
            unsigned long long number = strtoull(value.c_str() + 6, &endptr, 10);
            if (endptr == value.c_str() || *endptr != '-' || number == ULLONG_MAX)
            {
                return 0;
            }

            httpctx->rangeStart = number;
            if (length > (index + 1))
            {
                number = strtoull(value.c_str() + index + 1, &endptr, 10);
                if (endptr == value.c_str() || *endptr != '\0' || number == ULLONG_MAX)
                {
                    return 0;
                }
                httpctx->rangeEnd = number;
            }
            LOG_debug << "Range value parsed: " << httpctx->rangeStart << " - " << httpctx->rangeEnd;
        }
    }
    return 0;
}

int MegaHTTPServer::onBody(http_parser *, const char *, size_t)
{
    return 0;
}

int MegaHTTPServer::onMessageComplete(http_parser *parser)
{
    LOG_debug << "Message complete";
    MegaNode *node = NULL;
    std::ostringstream response;
    MegaHTTPContext* httpctx = (MegaHTTPContext*) parser->data;
    httpctx->bytesWritten = 0;
    httpctx->size = 0;
    httpctx->streamingBuffer.setMaxBufferSize(httpctx->server->getMaxBufferSize());
    httpctx->streamingBuffer.setMaxOutputSize(httpctx->server->getMaxOutputSize());

    httpctx->transfer = new MegaTransferPrivate(MegaTransfer::TYPE_LOCAL_HTTP_DOWNLOAD);
    httpctx->transfer->setPath(httpctx->path.c_str());
    if (httpctx->nodename.size())
    {
        httpctx->transfer->setFileName(httpctx->nodename.c_str());
    }
    if (httpctx->nodehandle.size())
    {
        httpctx->transfer->setNodeHandle(MegaApi::base64ToHandle(httpctx->nodehandle.c_str()));
    }
    httpctx->transfer->setStartTime(Waiter::ds);

    if (parser->method == HTTP_OPTIONS)
    {
        LOG_debug << "Request method: OPTIONS";
        response << "HTTP/1.1 200 OK\r\n"
                    "Allow: GET,POST,HEAD,OPTIONS\r\n"
                    "Connection: close\r\n"
                    "\r\n";

        httpctx->resultCode = API_OK;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        return 0;
    }

    if (parser->method != HTTP_GET && parser->method != HTTP_POST && parser->method != HTTP_HEAD)
    {
        LOG_debug << "Method not allowed: " << parser->method;
        response << "HTTP/1.1 405 Method not allowed\r\n"
                    "Connection: close\r\n"
                    "\r\n";

        httpctx->resultCode = 405;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        return 0;
    }
    else
    {
        switch (parser->method)
        {
        case HTTP_GET:
            LOG_debug << "Request method: GET";
            break;
        case HTTP_POST:
            LOG_debug << "Request method: POST";
            break;
        case HTTP_HEAD:
            LOG_debug << "Request method: HEAD";
            break;
        default:
            LOG_warn << "Request method: " << parser->method;
        }
    }

    if (httpctx->path == "/favicon.ico")
    {
        LOG_debug << "Favicon requested";
        response << "HTTP/1.1 301 Moved Permanently\r\n"
                    "Location: https://mega.nz/favicon.ico\r\n"
                    "Connection: close\r\n"
                    "\r\n";

        httpctx->resultCode = API_OK;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        return 0;
    }

    if (httpctx->path == "/")
    {
        node = httpctx->megaApi->getRootNode();
        char *base64Handle = node->getBase64Handle();
        httpctx->nodehandle = base64Handle;
        delete [] base64Handle;
        httpctx->nodename = node->getName();
        httpctx->transfer->setFileName(httpctx->nodename.c_str());
    }
    else if (httpctx->nodehandle.size())
    {
        node = httpctx->megaApi->getNodeByHandle(MegaApi::base64ToHandle(httpctx->nodehandle.c_str()));
    }

    if (!httpctx->nodehandle.size())
    {
        response << "HTTP/1.1 404 Not Found\r\n"
                    "Connection: close\r\n"
                  << "\r\n";
        httpctx->resultCode = 404;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        return 0;
    }

    handle h = MegaApi::base64ToHandle(httpctx->nodehandle.c_str());
    if (!httpctx->server->isHandleAllowed(h))
    {
        LOG_debug << "Forbidden due to the restricted mode";
        response << "HTTP/1.1 403 Forbidden\r\n"
                    "Connection: close\r\n"
                  << "\r\n";

        httpctx->resultCode = 403;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        return 0;
    }

    if (!node)
    {
        if (!httpctx->nodehandle.size() || !httpctx->nodekey.size())
        {
            LOG_warn << "URL not found: " << httpctx->path;

            response << "HTTP/1.1 404 Not Found\r\n"
                        "Connection: close\r\n"
                      << "\r\n";

            httpctx->resultCode = 404;
            string resstr = response.str();
            sendHeaders(httpctx, &resstr);
            return 0;
        }
        else
        {
            string link = "https://mega.nz/#!";
            link.append(httpctx->nodehandle);
            link.append("!");
            link.append(httpctx->nodekey);
            LOG_debug << "Getting public link: " << link;
            httpctx->megaApi->getPublicNode(link.c_str(), httpctx);
            return 0;
        }
    }

    if (node && httpctx->nodename != node->getName())
    {
        //Subtitles support
        bool subtitles = false;

        if (httpctx->server->isSubtitlesSupportEnabled())
        {
            string originalname = node->getName();
            string::size_type dotpos = originalname.find_last_of('.');
            if (dotpos != string::npos)
            {
                originalname.resize(dotpos);
            }

            if (dotpos == httpctx->nodename.find_last_of('.') && !memcmp(originalname.data(), httpctx->nodename.data(), originalname.size()))
            {
                LOG_debug << "Possible subtitles file";
                MegaNode *parent = httpctx->megaApi->getParentNode(node);
                if (parent)
                {
                    MegaNode *child = httpctx->megaApi->getChildNode(parent, httpctx->nodename.c_str());
                    if (child)
                    {
                        LOG_debug << "Matching file found: " << httpctx->nodename << " - " << node->getName();
                        subtitles = true;
                        delete node;
                        node = child;
                    }
                    delete parent;
                }
            }
        }

        if (!subtitles)
        {
            LOG_warn << "Invalid name: " << httpctx->nodename << " - " << node->getName();

            response << "HTTP/1.1 404 Not Found\r\n"
                        "Connection: close\r\n"
                      << "\r\n";

            httpctx->resultCode = 404;
            string resstr = response.str();
            sendHeaders(httpctx, &resstr);
            delete node;
            return 0;
        }
    }

    if (node->isFolder())
    {
        if (!httpctx->server->isFolderServerEnabled())
        {
            response << "HTTP/1.1 403 Forbidden\r\n"
                        "Connection: close\r\n"
                      << "\r\n";

            httpctx->resultCode = 403;
            string resstr = response.str();
            sendHeaders(httpctx, &resstr);
            delete node;
            return 0;
        }

        MegaNode *parent = httpctx->megaApi->getParentNode(node);
        MegaNodeList *children = httpctx->megaApi->getChildren(node);

        std::ostringstream web;

        // Title
        web << "<title>MEGA</title>";

        //Styles
        web << "<head><style>"
               ".folder {"
               "padding: 0;"
               "width: 24px;"
               "height: 24px;"
               "margin: 0 0 0 -2px;"
               "display: block;"
               "position: absolute;"
               "background-image: url(https://eu.static.mega.co.nz/3/images/mega/nw-fm-sprite_v12.svg);"
               "background-position: -14px -7465px;"
               "background-repeat: no-repeat;}"

               ".file {"
               "padding: 0;"
               "width: 24px;"
               "height: 24px;"
               "margin: 0 0 0 -6px;"
               "display: block;"
               "position: absolute;"
               "background-image: url(https://eu.static.mega.co.nz/3/images/mega/nw-fm-sprite_v12.svg);"
               "background-position: -7px -1494px;"
               "background-repeat: no-repeat;} "

               ".headerimage {"
               "padding: 0 8px 0 46px;"
               "width: 100%;"
               "height: 24px;"
               "margin: 0 0 0 -12px;"
               "display: block;"
               "position: absolute;"
               "background-image: url(https://eu.static.mega.co.nz/3/images/mega/nw-fm-sprite_v12.svg);"
               "background-position: 5px -1000px;"
               "line-height: 23px;"
               "background-repeat: no-repeat;} "

               ".headertext {"
               "line-height: 23px;"
               "color: #777777;"
               "font-size: 18px;"
               "font-weight: bold;"
               "display: block;"
               "position: absolute;"
               "line-height: 23px;}"

               "a {"
               "text-decoration: none; }"

               ".text {"
               "height: 24px;"
               "padding: 0 10px 0 26px;"
               "word-break: break-all;"
               "white-space: pre-wrap;"
               "overflow: hidden;"
               "max-width: 100%;"
               "text-decoration: none;"
               "-moz-box-sizing: border-box;"
               "-webkit-box-sizing: border-box;"
               "box-sizing: border-box;"
               "font-size: 13px;"
               "line-height: 23px;"
               "color: #666666;}"
               "</style></head>";

        // Folder path
        web << "<span class=\"headerimage\"><span class=\"headertext\">";
        char *path = httpctx->megaApi->getNodePath(node);
        if (path)
        {
            web << path;
            delete [] path;
        }
        else
        {
            web << node->getName();
        }
        web << "</span></span><br /><br />";

        // Child nodes
        web << "<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"width: auto;\">";
        if (parent)
        {
            web << "<tr><td>";
            char *base64Handle = parent->getBase64Handle();
            web << "<a href=\"/" << base64Handle << "/" << parent->getName()
                << "\"><span class=\"folder\"></span><span class=\"text\">..</span></a>";
            delete [] base64Handle;
            delete parent;
            web << "</td></tr>";
        }

        for (int i = 0; i < children->size(); i++)
        {
            web << "<tr><td>";
            MegaNode *child = children->get(i);
            char *base64Handle = child->getBase64Handle();
            web << "<a href=\"/" << base64Handle << "/" << child->getName()
                << "\"><span class=\"" << (child->isFile() ? "file" : "folder") << "\"></span><span class=\"text\">"
                << child->getName() << "</span></a>";
            delete [] base64Handle;

            if (!child->isFile())
            {
                web << "</td><td>";
            }
            else
            {
                unsigned const long long KB = 1024;
                unsigned const long long MB = 1024 * KB;
                unsigned const long long GB = 1024 * MB;
                unsigned const long long TB = 1024 * GB;

                web << "</td><td><span class=\"text\">";
                unsigned long long bytes = child->getSize();
                if (bytes > TB)
                    web << ((unsigned long long)((100 * bytes) / TB))/100.0 << " TB";
                else if (bytes > GB)
                    web << ((unsigned long long)((100 * bytes) / GB))/100.0 << " GB";
                else if (bytes > MB)
                    web << ((unsigned long long)((100 * bytes) / MB))/100.0 << " MB";
                else if (bytes > KB)
                    web << ((unsigned long long)((100 * bytes) / KB))/100.0 << " KB";
                web << "</span>";
            }
            web << "</td></tr>";
        }
        web << "</table>";
        delete children;

        string sweb = web.str();
        response << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: text/html\r\n"
            << "Connection: close\r\n"
            << "Content-Length: " << sweb.size() << "\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "\r\n";

        if (httpctx->parser.method != HTTP_HEAD)
        {
            response << sweb;
        }
        httpctx->resultCode = API_OK;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        return 0;
    }

    //File node
    if (!httpctx->server->isFileServerEnabled())
    {
        response << "HTTP/1.1 403 Forbidden\r\n"
                    "Connection: close\r\n"
                  << "\r\n";

        httpctx->resultCode = 403;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        delete node;
        return 0;
    }

    httpctx->node = node;
    streamNode(httpctx);
    return 0;
}

int MegaHTTPServer::streamNode(MegaHTTPContext *httpctx)
{
    std::ostringstream response;
    MegaNode *node = httpctx->node;

    string name;
    const char *extension = NULL;
    const char *nodeName = httpctx->node->getName();
    if (nodeName)
    {
        name = nodeName;
    }

    string::size_type dotindex = name.find_last_of('.');
    if (dotindex != string::npos)
    {
        extension = name.c_str() + dotindex;
    }

    char *mimeType = MegaApi::getMimeType(extension);
    if (!mimeType)
    {
        mimeType = MegaApi::strdup("application/octet-stream");
    }

    m_off_t totalSize = node->getSize();
    m_off_t start = 0;
    m_off_t end = totalSize - 1;
    if (httpctx->rangeStart >= 0)
    {
        start = httpctx->rangeStart;
    }
    httpctx->rangeStart = start;

    if (httpctx->rangeEnd >= 0)
    {
        end = httpctx->rangeEnd;
    }
    httpctx->rangeEnd = end + 1;

    bool rangeRequested = (httpctx->rangeEnd - httpctx->rangeStart) != totalSize;

    m_off_t len = end - start + 1;
    if (start < 0 || start >= totalSize || end < 0 || end >= totalSize || len <= 0 || len > totalSize)
    {
        response << "HTTP/1.1 416 Requested Range Not Satisfiable\r\n"
            << "Content-Type: " << mimeType << "\r\n"
            << "Connection: close\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Accept-Ranges: bytes\r\n"
            << "Content-Range: bytes 0-0/" << totalSize << "\r\n"
            << "\r\n";

        delete [] mimeType;
        httpctx->resultCode = 416;
        string resstr = response.str();
        sendHeaders(httpctx, &resstr);
        return 0;
    }

    if (rangeRequested)
    {
        response << "HTTP/1.1 206 Partial Content\r\n";
        response << "Content-Range: bytes " << start << "-" << end << "/" << totalSize << "\r\n";
    }
    else
    {
        response << "HTTP/1.1 200 OK\r\n";
    }

    response << "Content-Type: " << mimeType << "\r\n"
        << "Connection: close\r\n"
        << "Content-Length: " << len << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Accept-Ranges: bytes\r\n"
        << "\r\n";

    delete [] mimeType;
    httpctx->pause = false;
    httpctx->lastBuffer = NULL;
    httpctx->lastBufferLen = 0;
    httpctx->transfer->setStartPos(start);
    httpctx->transfer->setEndPos(end);

    string resstr = response.str();
    if (httpctx->parser.method != HTTP_HEAD)
    {
        httpctx->streamingBuffer.init(len + resstr.size());
        httpctx->size = len;
    }
    sendHeaders(httpctx, &resstr);
    if (httpctx->parser.method == HTTP_HEAD)
    {
        return 0;
    }

    LOG_debug << "Requesting range. From " << start << "  size " << len;
    uv_mutex_init(&httpctx->mutex);
    httpctx->rangeWritten = 0;
    httpctx->megaApi->startStreaming(node, start, len, httpctx);
    return 0;
}

void MegaHTTPServer::sendHeaders(MegaHTTPContext *httpctx, string *headers)
{
    LOG_debug << "Response headers: " << *headers;
    httpctx->streamingBuffer.append(headers->data(), headers->size());
    uv_buf_t resbuf = httpctx->streamingBuffer.nextBuffer();
    httpctx->size += headers->size();
    httpctx->lastBuffer = resbuf.base;
    httpctx->lastBufferLen = resbuf.len;

    httpctx->transfer->setTotalBytes(httpctx->size);
    httpctx->megaApi->fireOnStreamingStart(httpctx->transfer);

    uv_write_t *req = new uv_write_t;
    req->data = httpctx;
    if (int err = uv_write(req, (uv_stream_t*)&httpctx->tcphandle, &resbuf, 1, onWriteFinished))
    {
        LOG_warn << "Finishing due to an error sending the response: " << err;
        httpctx->finished = true;
        if (!uv_is_closing((uv_handle_t*)&httpctx->tcphandle))
        {
            uv_close((uv_handle_t*)&httpctx->tcphandle, onClose);
        }
    }
}

void MegaHTTPServer::onAsyncEvent(uv_async_t* handle)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) handle->data;
    if (httpctx->failed)
    {
        LOG_warn << "Streaming transfer failed. Closing connection.";
        if (!uv_is_closing((uv_handle_t*)&httpctx->tcphandle))
        {
            uv_close((uv_handle_t*)&httpctx->tcphandle, onClose);
        }
        return;
    }

    if (httpctx->nodereceived)
    {
        httpctx->nodereceived = false;
        if (!httpctx->node || httpctx->nodename != httpctx->node->getName())
        {
            if (!httpctx->node)
            {
                LOG_warn << "Public link not found";
            }
            else
            {
                LOG_warn << "Invalid name for public link";
            }

            httpctx->resultCode = 404;
            string resstr = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
            sendHeaders(httpctx, &resstr);
            return;
        }

        streamNode(httpctx);
        return;
    }

    sendNextBytes(httpctx);
}

void MegaHTTPServer::onCloseRequested(uv_async_t *handle)
{
    LOG_debug << "HTTP server stopping";
    MegaHTTPServer *httpServer = (MegaHTTPServer*) handle->data;

    for (list<MegaHTTPContext*>::iterator it = httpServer->connections.begin(); it != httpServer->connections.end(); it++)
    {
        MegaHTTPContext *httpctx = (*it);
        httpctx->finished = true;
        if (!uv_is_closing((uv_handle_t*)&httpctx->tcphandle))
        {
            uv_close((uv_handle_t *)&httpctx->tcphandle, onClose);
        }
    }

    uv_close((uv_handle_t *)&httpServer->server, NULL);
    uv_close((uv_handle_t *)&httpServer->exit_handle, NULL);
}

void MegaHTTPServer::sendNextBytes(MegaHTTPContext *httpctx)
{
    uv_mutex_lock(&httpctx->mutex);
    if (httpctx->lastBuffer)
    {
        LOG_verbose << "Skipping write due to another ongoing write";
        uv_mutex_unlock(&httpctx->mutex);
        return;
    }

    if (httpctx->lastBufferLen)
    {
        httpctx->streamingBuffer.freeData(httpctx->lastBufferLen);
        httpctx->lastBufferLen = 0;
    }

    if (httpctx->tcphandle.write_queue_size > httpctx->streamingBuffer.availableCapacity() / 8)
    {
        LOG_warn << "Skipping write. Too much queued data";
        uv_mutex_unlock(&httpctx->mutex);
        return;
    }

    uv_buf_t resbuf = httpctx->streamingBuffer.nextBuffer();
    if (!resbuf.len)
    {
        LOG_verbose << "Skipping write. No data available";
        uv_mutex_unlock(&httpctx->mutex);
        return;
    }

    uv_write_t *req = new uv_write_t;
    req->data = httpctx;

    LOG_verbose << "Writting " << resbuf.len << " bytes";
    httpctx->rangeWritten += resbuf.len;
    httpctx->lastBuffer = resbuf.base;
    httpctx->lastBufferLen = resbuf.len;
    if (int err = uv_write(req, (uv_stream_t*)&httpctx->tcphandle, &resbuf, 1, MegaHTTPServer::onWriteFinished))
    {
        LOG_warn << "Finishing due to an error in uv_write: " << err;
        httpctx->finished = true;
        if (!uv_is_closing((uv_handle_t*)&httpctx->tcphandle))
        {
            uv_close((uv_handle_t*)&httpctx->tcphandle, onClose);
        }
    }
    uv_mutex_unlock(&httpctx->mutex);
}

void MegaHTTPServer::onWriteFinished(uv_write_t* req, int status)
{
    MegaHTTPContext* httpctx = (MegaHTTPContext*) req->data;
    httpctx->bytesWritten += httpctx->lastBufferLen;
    LOG_verbose << "Bytes written: " << httpctx->lastBufferLen << " Remaining: " << (httpctx->size - httpctx->bytesWritten);
    httpctx->lastBuffer = NULL;
    delete req;

    if (status < 0 || httpctx->size == httpctx->bytesWritten)
    {
        if (status < 0)
        {
            LOG_warn << "Finishing request. Write failed: " << status;
        }
        else
        {
            LOG_debug << "Finishing request. All data sent";
            if (httpctx->resultCode == API_EINTERNAL)
            {
                httpctx->resultCode = API_OK;
            }
        }

        httpctx->finished = true;
        if (!uv_is_closing((uv_handle_t*)&httpctx->tcphandle))
        {
            uv_close((uv_handle_t*)&httpctx->tcphandle, onClose);
        }
        return;
    }

    if (httpctx->pause)
    {
        uv_mutex_lock(&httpctx->mutex);
        if (httpctx->streamingBuffer.availableSpace() > httpctx->streamingBuffer.availableCapacity() / 2)
        {
            httpctx->pause = false;
            m_off_t start = httpctx->rangeStart + httpctx->rangeWritten + httpctx->streamingBuffer.availableData();
            m_off_t len =  httpctx->rangeEnd - httpctx->rangeStart - httpctx->rangeWritten - httpctx->streamingBuffer.availableData();

            LOG_debug << "Resuming streaming from " << start << " len: " << len
                     << " Buffer status: " << httpctx->streamingBuffer.availableSpace()
                     << " of " << httpctx->streamingBuffer.availableCapacity() << " bytes free";
            httpctx->megaApi->startStreaming(httpctx->node, start, len, httpctx);
        }
        uv_mutex_unlock(&httpctx->mutex);
    }
    sendNextBytes(httpctx);
}


MegaHTTPContext::MegaHTTPContext()
{
    rangeStart = -1;
    rangeEnd = -1;
    range = false;
    finished = false;
    failed = false;
    nodereceived = false;
    resultCode = API_EINTERNAL;
    node = NULL;
    transfer = NULL;
}

void MegaHTTPContext::onTransferStart(MegaApi *, MegaTransfer *transfer)
{
    this->transfer->setTag(transfer->getTag());
}

bool MegaHTTPContext::onTransferData(MegaApi *, MegaTransfer *transfer, char *buffer, size_t size)
{
    LOG_verbose << "Streaming data received: " << transfer->getTransferredBytes()
                << " Size: " << size
                << " Queued: " << this->tcphandle.write_queue_size
                << " Buffered: " << streamingBuffer.availableData()
                << " Free: " << streamingBuffer.availableSpace();

    if (finished)
    {
        LOG_info << "Removing streaming transfer after " << transfer->getTransferredBytes() << " bytes";
        return false;
    }

    // append the data to the buffer
    uv_mutex_lock(&mutex);
    if (streamingBuffer.availableSpace() < 2 * size)
    {
        LOG_debug << "Buffer full: " << streamingBuffer.availableSpace() << " of "
                 << streamingBuffer.availableCapacity() << " bytes available only. Pausing streaming";
        pause = true;
    }
    streamingBuffer.append(buffer, size);
    uv_mutex_unlock(&mutex);

    // notify the HTTP server
    uv_async_send(&asynchandle);
    return !pause;
}

void MegaHTTPContext::onTransferFinish(MegaApi *, MegaTransfer *, MegaError *e)
{
    int ecode = e->getErrorCode();
    if (ecode != API_OK && ecode != API_EINCOMPLETE)
    {
        LOG_warn << "Transfer failed with error code: " << ecode;
        failed = true;
        finished = true;
        uv_async_send(&asynchandle);
    }
}

void MegaHTTPContext::onRequestFinish(MegaApi *, MegaRequest *request, MegaError *)
{
    node = request->getPublicMegaNode();
    nodereceived = true;
    uv_async_send(&asynchandle);
}
#endif

#ifdef ENABLE_CHAT
MegaTextChatPeerListPrivate::MegaTextChatPeerListPrivate()
{

}

MegaTextChatPeerListPrivate::~MegaTextChatPeerListPrivate()
{

}

MegaTextChatPeerList *MegaTextChatPeerListPrivate::copy() const
{
    MegaTextChatPeerListPrivate *ret = new MegaTextChatPeerListPrivate;

    for (int i = 0; i < size(); i++)
    {
        ret->addPeer(list.at(i).first, list.at(i).second);
    }

    return ret;
}

void MegaTextChatPeerListPrivate::addPeer(MegaHandle h, int priv)
{
    list.push_back(userpriv_pair(h, (privilege_t) priv));
}

MegaHandle MegaTextChatPeerListPrivate::getPeerHandle(int i) const
{
    if (i > size())
    {
        return INVALID_HANDLE;
    }
    else
    {
        return list.at(i).first;
    }
}

int MegaTextChatPeerListPrivate::getPeerPrivilege(int i) const
{
    if (i > size())
    {
        return PRIV_UNKNOWN;
    }
    else
    {
        return list.at(i).second;
    }
}

int MegaTextChatPeerListPrivate::size() const
{
    return list.size();
}

const userpriv_vector *MegaTextChatPeerListPrivate::getList() const
{
    return &list;
}

MegaTextChatPeerListPrivate::MegaTextChatPeerListPrivate(userpriv_vector *userpriv)
{
    handle uh;
    privilege_t priv;

    for (unsigned i = 0; i < userpriv->size(); i++)
    {
        uh = userpriv->at(i).first;
        priv = userpriv->at(i).second;

        this->addPeer(uh, priv);
    }
}

MegaTextChatPrivate::MegaTextChatPrivate(const MegaTextChat *chat)
{
    this->id = chat->getHandle();
    this->priv = chat->getOwnPrivilege();
    this->url = chat->getUrl();
    this->shard = chat->getShard();
    this->peers = chat->getPeerList() ? chat->getPeerList()->copy() : NULL;
    this->group = chat->isGroup();
    this->ou = chat->getOriginatingUser();
}

MegaTextChatPrivate::MegaTextChatPrivate(handle id, int priv, string url, int shard, const MegaTextChatPeerList *peers, bool group, handle ou)
{
    this->id = id;
    this->priv = priv;
    this->url = url;
    this->shard = shard;
    this->peers = peers ? peers->copy() : NULL;
    this->group = group;
    this->ou = ou;
}

MegaTextChatPrivate::~MegaTextChatPrivate()
{
    delete peers;
}

MegaHandle MegaTextChatPrivate::getHandle() const
{
    return id;
}

int MegaTextChatPrivate::getOwnPrivilege() const
{
    return priv;
}

const char *MegaTextChatPrivate::getUrl() const
{
    return url.c_str();
}

void MegaTextChatPrivate::setUrl(const char *url)
{
    if (url)
    {
        this->url.assign(url);
    }
    else
    {
        this->url.clear();
    }
}

int MegaTextChatPrivate::getShard() const
{
    return shard;
}

const MegaTextChatPeerList *MegaTextChatPrivate::getPeerList() const
{
    return peers;
}

bool MegaTextChatPrivate::isGroup() const
{
    return group;
}

MegaHandle MegaTextChatPrivate::getOriginatingUser() const
{
    return ou;
}

MegaTextChatListPrivate::~MegaTextChatListPrivate()
{
    for (int i = 0; i < size(); i++)
    {
        delete list.at(i);
    }
}

MegaTextChatList *MegaTextChatListPrivate::copy() const
{
    return new MegaTextChatListPrivate(this);
}

const MegaTextChat *MegaTextChatListPrivate::get(int i) const
{
    if (i >= size())
    {
        return NULL;
    }
    else
    {
        return list.at(i);
    }
}

MegaTextChat *MegaTextChatListPrivate::get(int i)
{
    if (i >= size())
    {
        return NULL;
    }
    else
    {
        return list.at(i);
    }
}

int MegaTextChatListPrivate::size() const
{
    return list.size();
}

void MegaTextChatListPrivate::addChat(MegaTextChatPrivate *chat)
{
    list.push_back(chat);
}

MegaTextChatListPrivate::MegaTextChatListPrivate(const MegaTextChatListPrivate *list)
{
    MegaTextChatPrivate *chat;

    for (int i = 0; i < list->size(); i++)
    {
        chat = new MegaTextChatPrivate(list->get(i));
        this->list.push_back(chat);
    }
}

MegaTextChatListPrivate::MegaTextChatListPrivate()
{

}

MegaTextChatListPrivate::MegaTextChatListPrivate(textchat_vector *list)
{
    MegaTextChatPrivate *megaChat;
    MegaTextChatPeerListPrivate *chatPeers;
    TextChat *chat;

    for (unsigned i = 0; i < list->size(); i++)
    {
        chat = list->at(i);
        chatPeers = chat->userpriv ? new MegaTextChatPeerListPrivate(chat->userpriv) : NULL;
        megaChat = new MegaTextChatPrivate(chat->id, chat->priv, chat->url, chat->shard, chatPeers, chat->group, chat->ou);

        this->list.push_back(megaChat);
    }
}
#endif


PublicLinkProcessor::PublicLinkProcessor()
{

}

bool PublicLinkProcessor::processNode(Node *node)
{
    if(!node->outshares)
    {
        return true;
    }

    for (share_map::iterator it = node->outshares->begin(); it != node->outshares->end(); it++)
    {
        Share *share = it->second;
        if (share->user == NULL)    // public links have no user
        {
            nodes.push_back(node);
        }
    }

    return true;
}

PublicLinkProcessor::~PublicLinkProcessor() {}

vector<Node *> &PublicLinkProcessor::getNodes()
{
    return nodes;
}
