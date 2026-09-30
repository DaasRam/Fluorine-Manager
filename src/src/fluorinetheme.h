#pragma once

class QWidget;

namespace FluorineTheme
{
// Scoped to a Fluorine surface. Uses the active application's palette so light,
// dark and custom themes retain their contrast and the user's font settings.
void apply(QWidget* surface, bool compact = false);
}
