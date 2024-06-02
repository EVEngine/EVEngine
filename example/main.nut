// This is the main entry point for the game
// It is responsible for setting up the game and running the game loop



class Position extends eve::Component {
    x = eve::Number;
    y = eve::Number;
}

class Velocity extends eve::Component {
    x = eve::Number;
    y = eve::Number;
}

class Collision extends eve::Component {
    hit = eve::Boolean;
}



class PhysicsSystem extends eve::System {
    update(dt) {
        for (let entity of this.entities) {
            let pos = entity.getComponent(Position)
            let vel = entity.getComponent(Velocity)

            pos.x += vel.x * dt
            pos.y += vel.y * dt
        }
    }

    computeNewPosition(pos, vel, dt) {
        return {
            x: pos.x + vel.x * dt,
            y: pos.y + vel.y * dt
        }
    }
    
    computeCollision(pos1, pos2) {
        return pos1.x == pos2.x && pos1.y == pos2.y
    }

    computeNewVelocity(vel) {
        return {
            x: vel.x,
            y: vel.y
        }
    }
}


class Moveable extends eve::EntityContainer {
    pos = new Position()
    vel = new Velocity()
}


class Bullets extends eve::EntityContainer {
    pos = new Position()
    vel = new Velocity()

}

class Enemies extends eve::EntityContainer {
    pos = new Position()
    vel = new Velocity()
    collision = new Collision()
}


class Player extends eve::GameObject {
    update() {
        
    }
}


// You can define init, update and render functions and use the default main function
function eve::init() {

}

function eve::update(dt) {
    // Update the game state

}

function eve::render() {
    // Render the game 

}


// OR, You can define a main function as the entry point for your game
function eve::main() {
    // Do anything you want   
}