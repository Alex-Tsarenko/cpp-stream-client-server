//
//  main.cpp
//  Streaming
//
//  Created by Aleksander Tsarenko on 04.02.2021.
//

#include <unistd.h>

#include <iostream>
#include "AsyncTcpServer.h"
#include "StreamClient.h"
#include "StreamManager.h"

using namespace catapult::net;
using namespace catapult::streaming;

int main(int, const char * [])
{
    std::string errorText;
    gStreamManager().startStreamManager( 15001, errorText );

    return 0;
}

