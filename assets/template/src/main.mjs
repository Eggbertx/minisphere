/*
 *  <title of your new Sphere game goes here>
 *  (c) <year> <whoever made the game>
 */

import { Music, Prim, Thread } from 'sphere-runtime';

export default
class MyGame extends Thread
{
	constructor()
	{
		super();  // call the superclass constructor

		/*
		 *  put code in here to initialize your game on startup--setting up
		 *  data, loading resources, etc.  use `this` to associate things with
		 *  the Game object, or `global` to make global variables that are
		 *  accessible game-wide.
		 */

		// initialize data for our session
		this.image = new Texture('@/images/justSaiyan.png');
		this.x = 0;
		this.y = 0;
		this.xVel = 1;
		this.yVel = 1;

		// avoid boredom by playing some background music!
		Music.play('@/music/vegetaSSj.ogg');
	}

	on_update()
	{
		/*
		 *  put code in here to update game state, for example moving character
		 *  sprites or updating animations.  this will be called once per frame
		 *  at a rate determined by the value of screen.frameRate.
		 */

		this.x += this.xVel;
		this.y += this.yVel;
		if (this.x <= 0) {
			this.x = 0;
			this.xVel = 1;
		}
		else if (this.x >= Surface.Screen.width - this.image.width) {
			this.x = Surface.Screen.width - this.image.width;
			this.xVel = -1;
		}
		if (this.y <= 0) {
			this.y = 0;
			this.yVel = 1;
		}
		else if (this.y >= Surface.Screen.height - this.image.height) {
			this.y = Surface.Screen.height - this.image.height;
			this.yVel = -1;
		}
	}

	on_render()
	{
		/*
		 *  put code in here to draw things each frame.  don't do anything
		 *  other than drawing-related things here, as render calls can be
		 *  skipped and are not guaranteed to match the frame rate.
		 */

		Prim.fill(Surface.Screen, Color.DodgerBlue);
		Prim.drawSolidEllipse(Surface.Screen,
			Surface.Screen.width / 2, Surface.Screen.height / 2,
			Surface.Screen.width / 4, Surface.Screen.height / 4,
			Color.Chartreuse, Color.DarkGreen);
		Prim.drawEllipse(Surface.Screen,
			Surface.Screen.width / 2, Surface.Screen.height / 2,
			Surface.Screen.width / 4, Surface.Screen.height / 4,
			Color.Black);

		Prim.blit(Surface.Screen, this.x, this.y, this.image);
		Prim.drawRectangle(Surface.Screen, this.x, this.y, this.image.width, this.image.height, 2, Color.Black);
	}
}
