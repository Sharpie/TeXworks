/*
  This is part of TeXworks, an environment for working with TeX documents
  Copyright (C) 2007-2011  Jonathan Kew, Stefan Löffler, Charlie Sharpsteen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  For links to further information, or to contact the author,
  see <http://www.tug.org/texworks/>.
*/

#ifndef TWCoffeePlugin_H
#define TWCoffeePlugin_H

#include "TWScript.h"

#include <QMetaMethod>
#include <QMetaProperty>

class TWCoffeeScriptPlugin : public QObject, public TWScriptLanguageInterface
{
  Q_OBJECT
  Q_INTERFACES(TWScriptLanguageInterface)

public:
  TWCoffeeScriptPlugin() {};
  virtual ~TWCoffeeScriptPlugin() {};

  virtual TWScript* newScript(const QString& fileName);

  virtual QString scriptLanguageName() const { return QString("CoffeeScript"); }
  virtual QString scriptLanguageURL() const { return QString("http://coffeescript.org"); }
  virtual bool canHandleFile(const QFileInfo& fileInfo) const { return fileInfo.suffix() == QString("coffee"); }
};


class CoffeeScript : public TWScript
{
  Q_OBJECT
  Q_INTERFACES(TWScript)

public:
  CoffeeScript(QObject * plugin, const QString& filename)
    : TWScript(plugin, filename) { }

  virtual bool parseHeader() { return doParseHeader("", "", "#"); };

protected:
  virtual bool execute(TWScriptAPI *tw) const;
};

#endif // End header guard
