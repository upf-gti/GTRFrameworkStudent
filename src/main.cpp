/*  by Javi Agenjo 2013 UPF  javi.agenjo@gmail.com

	MAIN:
	 + This file creates the window and the application instance. 
	 + It also contains the mainloop
	 + This is the lowest level, here we access the system to create the opengl Context
	 + It takes all the events from SDL and redirect them to the application

	D�dac Hierro Soteras
	Marc Bosch Manzano

	Object Properties: texture, material, mesh
	Liht information
	Light transport simulation
	Light sensors and lens
*/

#include "litengine.h"
#include "application.h"
#include <iostream> //to output

Application* app = NULL;

// *********************************

//The application main loop
int main(int argc, char **argv)
{
	std::cout << "Initiating app..." << std::endl;
	CORE::init();

	//define window size
	bool fullscreen = false; 
	Vector2f size(1024, 576); //1024,768 default
	if (fullscreen)
		size = CORE::getDesktopSize(0);

	//create the application window 
	CORE::Window* window = CORE::createWindow("GTR", (int)size.x, (int)size.y, fullscreen );
	if (!window)
		return 0;

	//create the app
	app = new Application();

	//main loop, application gets inside here till user closes it
	CORE::mainLoop(window,app);

	//save state and free memory
	CORE::destroy();

	return 0;
}
