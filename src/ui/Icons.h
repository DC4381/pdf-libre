// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QIcon>

// Icônes de la barre d'outils, dessinées en vectoriel : nettes à tous les
// facteurs d'échelle Windows (100 % à 250 %), sans fichier image à livrer.
namespace Icons {
enum class Kind {
    Open, Print, Find, PreviousPage, NextPage, Sidebar, ZoomIn, ZoomOut, Pages,
    Cursor, Highlight, Underline, StrikeOut, Note, Text, Pen, Line, Arrow, Rectangle, Ellipse, Stamp, Signature, Undo, Redo, Save
};
QIcon make(Kind kind);
}
