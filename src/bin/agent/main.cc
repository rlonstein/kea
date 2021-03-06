// Copyright (C) 2016 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>
#include <agent/ctrl_agent_controller.h>
#include <exceptions/exceptions.h>
#include <cstdlib>
#include <iostream>

using namespace isc::agent;
using namespace isc::process;

int main(int argc, char* argv[]) {
    int ret = EXIT_SUCCESS;

    // Launch the controller passing in command line arguments.
    // Exit program with the controller's return code.
    try  {
        // Instantiate/fetch the application controller singleton.
        DControllerBasePtr& controller = CtrlAgentController::instance();

        // 'false' value disables test mode.
        controller->launch(argc, argv, false);
    } catch (const VersionMessage& ex) {
        std::cout << ex.what() << std::endl;
    } catch (const isc::Exception& ex) {
        std::cerr << "Service failed:" << ex.what() << std::endl;
        ret = EXIT_FAILURE;
    }

    return (ret);
}
