#include"renderer.h"
#include<iostream>
int main(int argc, char* argv[]){

    SDL_Init(SDL_INIT_EVERYTHING);
    
    try{
        Renderer renderer;
        while(renderer.tick()){
            
        }
    }
    catch(std::runtime_error err){
        SDL_Quit();
        std::cerr<<err.what()<<std::endl;
    }


    
    SDL_Quit();
    return 0;
}