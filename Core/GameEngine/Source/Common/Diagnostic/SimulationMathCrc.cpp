/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cmath>
#include "PreRTS.h"

#include "Common/Diagnostic/SimulationMathCrc.h"
#include "Common/XferCRC.h"
#include "GameLogic/FPUControl.h"

import Engine.Core.Math.AffineTransform3;

static void appendSimulationMathCrc(XferCRC &xfer)
{
    Engine::Math::AffineTransform3 matrix;
    Engine::Math::AffineTransform3 factors_matrix;

    matrix.elements = {
        4.1f, 1.2f, 0.3f, 0.4f,
        0.5f, 3.6f, 0.7f, 0.8f,
        0.9f, 1.0f, 2.1f, 1.2f};

    factors_matrix.elements = {
        std::sin(0.7f) * std::log10(2.3f),
        std::cos(1.1f) * std::pow(1.1f, 2.0f),
        std::tan(0.3f),
        std::asin(0.967302263f),
        std::acos(0.967302263f),
        std::atan(0.967302263f) * std::pow(1.1f, 2.0f),
        std::atan2(0.4f, 1.3f),
        std::sinh(0.2f),
        std::cosh(0.4f) * std::tanh(0.5f),
        std::sqrt(55788.84375f),
        std::exp(0.1f) * std::log10(2.3f),
        std::log(1.4f)};

    matrix = Compose(matrix, factors_matrix);
    if (const auto inverse = matrix.Inverse())
        matrix = *inverse;

    // Match the legacy matrix transfer record: version followed by 12 row-major reals.
    XferVersion version = 1;
    xfer.xferVersion(&version, version);
    for (float &element : matrix.elements)
        xfer.xferReal(&element);
}

UnsignedInt SimulationMathCrc::calculate()
{
    XferCRC xfer;
    xfer.open("SimulationMathCrc");

    setFPMode();

    appendSimulationMathCrc(xfer);

    _fpreset();

    xfer.close();

    return xfer.getCRC();
}
