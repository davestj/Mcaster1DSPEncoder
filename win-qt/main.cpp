/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * main.cpp — Application entry point
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "app.h"

int main(int argc, char *argv[])
{
    mc1::App app(argc, argv);
    return app.exec();
}
