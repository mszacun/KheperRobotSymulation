#include "CommunicationManager.h"

const int CommunicationManager::NUMBER_OF_CONTROLLER_COMMANDS = 
	ClientCommand::MOTORS_SPEED_CHANGE_COMMAND_ID + 1;

CommunicationManager::CommunicationManager(DistrSimulation* sim) : _isStopped(false), _simulation(sim)
{
	InitializeCriticalSection(&_clientsMutex);

	// initialize arrays with clients commands
    _validControllerCommands = new ClientCommand*[NUMBER_OF_CONTROLLER_COMMANDS];
    _validControllerCommands[ClientCommand::SINGLE_MOTOR_SPEED_CHANGE_COMMAND_ID] =
		new SingleMotorSpeedChangeCommand();
    _validControllerCommands[ClientCommand::MOTORS_SPEED_CHANGE_COMMAND_ID] =
        new MotorsSpeedChangeCommand();
}

CommunicationManager::~CommunicationManager()
{
	/* close visualisers sockets */
	for (std::set<SOCKET>::iterator it = _visualisers.begin(); it != _visualisers.end(); it++)
	{
		shutdown(*it, SD_SEND);
		closesocket(*it);
	}

	// close robot controlers sockets
    for (std::map<int, SOCKET>::iterator it = _controllers.begin(); it != _controllers.end(); it++)
	{
		shutdown(it->second, SD_SEND);
		closesocket(it->second);
	}

	// delete commands
	for (int i = 0; i < NUMBER_OF_CONTROLLER_COMMANDS; i++)
        delete _validControllerCommands[i];

    delete[] _validControllerCommands;

	DeleteCriticalSection(&_clientsMutex);
}

bool CommunicationManager::init()
{
	struct addrinfo *result = NULL, *ptr = NULL, hints;

	ZeroMemory(&hints, sizeof (hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the server
	int iResult = getaddrinfo(NULL, LISTEN_PORT_STR, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		WSACleanup();
		return false;
	}

	// create socket
	_listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (_listenSocket == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return false;
	}

	// bind socket
	iResult = bind(_listenSocket, result->ai_addr, (int) result->ai_addrlen);
	freeaddrinfo(result);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		closesocket(_listenSocket);
		WSACleanup();
		return false;
	}

	// listen
	if (listen(_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
		printf("Listen failed with error: %ld\n", WSAGetLastError());
		closesocket(_listenSocket);
		WSACleanup();
		return false;
	}

	return true;
}

void CommunicationManager::runServerLoop()
{
	while (!_isStopped)
	{
		fd_set receivingSockets;
		FD_ZERO(&receivingSockets);

		// add controllers to observed sockets set
        for (std::map<int, SOCKET>::iterator it = _controllers.begin(); it != _controllers.end(); it++)
			FD_SET(it->second, &receivingSockets);

		// add visualisers
		for (std::set<SOCKET>::iterator it = _visualisers.begin(); it != _visualisers.end(); it++)
			FD_SET(*it, &receivingSockets);

		// add server's listen socket
		FD_SET(_listenSocket, &receivingSockets);

		int numberOfSockets = select(0, &receivingSockets, NULL, NULL, NULL);

		if (FD_ISSET(_listenSocket, &receivingSockets))
			accept_new_client();

		receive_controllers_messages(&receivingSockets);
		receive_visualisers_messages(&receivingSockets);
	}
}

void CommunicationManager::sendWorldDescriptionToVisualisers()
{
	Buffer buffer;
    _simulation->serialize(buffer);

	EnterCriticalSection(&_clientsMutex); // if server-thread add new client, iterator would be broken
	for (std::set<SOCKET>::iterator it = _visualisers.begin(); it != _visualisers.end(); it++)
        send(*it, reinterpret_cast<const char*>(buffer.getBuffer()), buffer.getLength(), 0);
	LeaveCriticalSection(&_clientsMutex);
}

void CommunicationManager::sendRobotsStatesToControllers()
{
    EnterCriticalSection(&_clientsMutex); // if server-thread add new client, iterator would be broken

    for (std::map<int, SOCKET>::iterator it = _controllers.begin(); it != _controllers.end(); it++)
    {
        Buffer buffer;
        dynamic_cast<KheperaRobot*>(_simulation->getEntity(it->first))->serializeForController(buffer);
        send(it->second, reinterpret_cast<const char*>(buffer.getBuffer()), buffer.getLength(), 0);
    }

    LeaveCriticalSection(&_clientsMutex);
}

bool CommunicationManager::accept_new_client()
{
	SOCKET ClientSocket = INVALID_SOCKET;
	// Accept a client socket
	ClientSocket = accept(_listenSocket, NULL, NULL);
	if (ClientSocket == INVALID_SOCKET)
    {
		printf("accept failed: %d\n", WSAGetLastError());
		closesocket(_listenSocket);
		WSACleanup();
		return false;
	}
	
	// receive new client type information, and it to appropriate container
	uint8_t newClientType;
	recv(ClientSocket, reinterpret_cast<char*>(&newClientType), 1, 0);

    unsigned long mode = 1;
    //ioctlsocket(ClientSocket, FIONBIO, &mode);
	if (newClientType == TYPE_ID_VISUALISER)
	{
		EnterCriticalSection(&_clientsMutex);
		    _visualisers.insert(ClientSocket);
		LeaveCriticalSection(&_clientsMutex);
	}
    else if (newClientType == TYPE_ID_CONTROLLER)
    {
        
        uint16_t controlledRobotId;
        recv(ClientSocket, reinterpret_cast<char*>(&controlledRobotId), sizeof(controlledRobotId), 0);
        controlledRobotId = ntohs(controlledRobotId);
        SimEnt* robot = _simulation->getEntity(controlledRobotId);
        if (robot != NULL && robot->getShapeID() == SimEnt::KHEPERA_ROBOT 
            && _controllers.find(controlledRobotId) == _controllers.end())
        {
            std::cout << "CONTROLLER FOR ROBOT WITH ID = " << controlledRobotId << " SUCCESSFULLY CONNECTED\n";
            EnterCriticalSection(&_clientsMutex);
                _controllers.insert(std::pair<int, SOCKET>(controlledRobotId, ClientSocket));
            LeaveCriticalSection(&_clientsMutex);
        }
        else
        {
            shutdown(ClientSocket, SD_RECEIVE);
            closesocket(ClientSocket);
            std::cout << "NO ROBOT WITH ID = " << controlledRobotId << " TO CONTROL.\n";
        }
    }

	return true;
}

void CommunicationManager::receive_controllers_messages(fd_set* sockets)
{
	// find who sent us a message
	std::map<int, SOCKET>::iterator it = _controllers.begin();
	while (it != _controllers.end())
	{
		if (FD_ISSET(it->second, sockets))
		{
			uint8_t commandID;
			int dataLength = recv(it->second, reinterpret_cast<char*>(&commandID), 1, 0);
            if (dataLength < 1)
            {
                std::cout << "REMOVING CONTROLLER" << std::endl;
                EnterCriticalSection(&_clientsMutex);
                    closesocket(it->second);
                    _controllers.erase(it++);
                LeaveCriticalSection(&_clientsMutex);
            }
			else
			{
				std::cout << "Received command id: " << commandID << std::endl;
				if (commandID < NUMBER_OF_CONTROLLER_COMMANDS)
					// TODO: Send back error code in case of errors
                    _validControllerCommands[commandID]->
                        execute(*_simulation->getEntity(it->first), *_simulation, it->second);
				it++;
			}
		}
		else
			it++;
	}
}

void CommunicationManager::receive_visualisers_messages(fd_set* sockets)
{
	// find who sent us a message
	std::set<SOCKET>::iterator it = _visualisers.begin();
	while (it != _visualisers.end())
	{
		if (FD_ISSET(*it, sockets))
		{
			uint8_t message;
			int dataLength = recv(*it, reinterpret_cast<char*>(&message), 1, 0);
            if (dataLength < 1)
            {
                std::cout << "REMOVING VISUALISER" << std::endl;
                EnterCriticalSection(&_clientsMutex);
                    closesocket(*it);
                    _visualisers.erase(it++);
                LeaveCriticalSection(&_clientsMutex);

            }
			else
			{
				// FIXME: For testing purpose only!
				std::cout << "Message from visualiser: " << message << std::endl;
				it++;
			}
		}
		else
			it++;
	}
}