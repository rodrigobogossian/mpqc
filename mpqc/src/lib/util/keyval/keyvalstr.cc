//
// keyvalstr.cc
//
// Copyright (C) 1996 Limit Point Systems, Inc.
//
// Author: Curtis Janssen <cljanss@limitpt.com>
// Maintainer: LPS
//
// This file is part of the SC Toolkit.
//
// The SC Toolkit is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// The SC Toolkit is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the SC Toolkit; see the file COPYING.LIB.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//
// The U.S. Government is granted a limited license as per AL 91-7.
//

extern "C" {
#include <ctype.h>
#include <stdlib.h>
}

#include <iostream.h>

#include <util/misc/formio.h>
#include <util/keyval/keyval.h>
#include <util/class/proxy.h>


////////////////////////////////////////////////////////////////////////
// StringKeyVal



StringKeyVal::StringKeyVal()
{

}

StringKeyVal::~StringKeyVal()
{
}

const char*
StringKeyVal::classname(const char * key)
{
  return 0;
}

const char*
StringKeyVal::truekeyword(const char * key)
{
  return key;
}

// This does not cause objects to be constructed.
int
StringKeyVal::key_exists(const char* key)
{
  stringvalue(key);
  if (error() == OK || error() == HasNoValue) {
      return 1;
    }
  return 0;
}

RefKeyValValue
StringKeyVal::key_value(const char* key, const KeyValValue &def)
{
  RefKeyValValue result;

  if (!key) key = "TOP";

  // convert the key to the true key so variable assignments in the
  // input will effectively be done by reference
  // check to see if the datum is a described class
  const char* tkw = truekeyword(key);
  //ExEnv::out() << "truekeyword = "<< tkw << '\n';
  if (!tkw) {
      result = 0;
    }
  else {
      // if a classname exists then read in the datum as an object
      const char* classn = classname(tkw);
      //ExEnv::out() << "classname = " << classn << '\n';
      if (classn) {
          KeyValKeyword truekey(tkw);
          if (_map.contains(truekey)) {
              result = _map[truekey];
            }
          else {
              // create a new instance of this datum
              RefKeyVal pkv = new PrefixKeyVal(this, tkw);
              const ClassDesc* cd = ClassDesc::name_to_class_desc(classn);
              if (!cd) {
                  ClassDesc::load_class(classn);
                  cd = ClassDesc::name_to_class_desc(classn);
                }
              // the original error status must be saved
              KeyValError original_error = error();
              RefDescribedClass newdc(cd->create(pkv));
              if (newdc.null()) {
                  ExEnv::err() << "StringKeyVal::value: create failed for:" << endl
                       << " keyword = \"" << tkw << "\" class = \"" << classn
                       << "\"" << endl
                       << " either the KeyVal create operator doesn't" << endl
                       << " exist or memory was exhausted" << endl;
                }
              DescribedClassProxy *proxy
                  = DescribedClassProxy::castdown(newdc.pointer());
              if (proxy) {
                  newdc = proxy->object();
                }
              seterror(original_error);
              KeyValValueRefDescribedClass* keyvalvalue
                  = new KeyValValueRefDescribedClass(newdc);
              _map[truekey] = keyvalvalue;
              result = keyvalvalue;
            }
        }
      else {
          const char* string = stringvalue(tkw);
          if (string) result = new KeyValValueString(string);
          else result = 0;
        }
    }

  if (verbose_) {
      ExEnv::out() << node0 << indent << key << " = ";
      if (result.null()) ExEnv::out() << node0 << def << " (default)";
      else ExEnv::out() << node0 << *result.pointer();
      ExEnv::out() << node0 << endl;
    }

  return result;
}

void
StringKeyVal::errortrace(ostream&o)
{
  o << indent << "StringKeyVal: error: \"" << errormsg() << "\"" << endl;
}

void
StringKeyVal::dump(ostream&o)
{
  o << indent << "StringKeyVal: error: \"" << errormsg() << "\"" << endl;
}

/////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: c++
// c-file-style: "CLJ"
// End:
