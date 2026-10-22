#pragma once
// Grand Prix award ceremony (Demo/Award.szs, Demo/Lose.szs; tools/cook_ceremony.py).
// A win first drives the top three down the winning run (course 55) in their own
// karts along the stage's three lanes, filmed by the stage's own KMP cameras
// (models/ceremony/winning_run.bin) until they pan up to the sky, then stands them
// on the podium (demowin03) as confetti falls. Otherwise the losing stage (course
// 56) shows them from afar. Over the podium: "Congratulations!" (or "Better luck
// next time!"), the cup and the rank grade. Last, the standings: the trophy in
// the sky, "You got Nth place!" and the twelve totals (drawn by the caller, see
// draw()). Text: models/ceremony/text.bin; award UI images. The SCN0 camera moves
// (models/ceremony/*.scam) drive the podium view. Podium spots are taken from the
// stand model, not retail code.
#include <mkw/animation.hpp>
#include <mkw/renderer.hpp>
#include <array>
#include <memory>
#include <vector>

namespace mkw {
struct GarageChoice;
class AwardCeremony {
public:
    struct Setup {
        // Kinoko::Character and Kinoko::Vehicle of first, second and third place.
        // Up to three on the podium, or a team's six (team).
        std::array<unsigned,6> character{},vehicle{};
        unsigned count=0;
        unsigned cup=0;     // cup index (Mushroom 0 .. Lightning 7)
        bool won=false;     // the player is in the top three
        unsigned points=0;  // the player's Grand Prix points (rank grade)
        int player=-1;      // the player's place among them (0..2), or -1
        unsigned place=1;   // the player's final place (1..12)
        unsigned engineClass=2; // 0 50cc, 1 100cc, 2 150cc, 3 Mirror
        // Team VS and Battle: no winning run and no podium; the winning team
        // stands on the flat award_v_stand2 and every member plays demowin.
        // won: the player's team won; winner 0 red, 1 blue; mode for the
        // standings' top line: 0 Balloon Battle, 1 Coin Runners, 2 Team VS.
        bool team=false;
        unsigned winner=0,mode=0;
        // Solo VS (this port's addition; retail has no VS ceremony): the GP podium
        // and run without the cup's trophy, cup line and rank grade.
        bool vs=false;
    };
    ~AwardCeremony();
    // Main thread, render thread idle and not deferred. On false the scene is
    // incomplete: destroy it (with the GPU idle) and skip the ceremony.
    bool load(const Setup& setup);
    // Advances 60 Hz ticks; false once the scene is over (skip() during the standings).
    bool step(unsigned ticks);
    // A whole top-screen frame: begin() through end(). During the standings
    // `standings` draws the totals board over it (before the fade).
    void draw(Renderer& renderer,void (*standings)(void*)=nullptr,void* context=nullptr);
    // The standings page is up (the caller steps and draws the board).
    bool standings() const {return standings_;}
    // Fade in at the start and out at the end (0 clear .. 1 black).
    float fade() const;
    bool won() const {return setup_.won;}
    // Retail lets A skip once the scene has settled.
    bool skippable() const {return tick_>=60;}
    void skip();
    // A, B or START: to the standings; on the standings, out.
    void advance(){if(standings_)skip();else{driving_=false;shot_=unsigned(cameras_.empty()?0:cameras_.size()-1);standings_=true;standingsTicks_=0;}}
    // The rank grade for a Grand Prix total (0 three stars .. 7 E).
    static unsigned grade(unsigned points);
private:
    struct Camera {std::vector<float> frames;unsigned count=0;unsigned hold=0;};
    // The winning run's KMP CAME chain (race_intro.hpp has the retail rules):
    // type 5 follows its POTI route and pans its view, type 4 holds its place,
    // type 2 follows its route watching the player's kart.
    struct DriveCamera {uint8_t type=0,next=0xff,route=0xff;uint16_t pathSpeed=0,fovSpeed=0,viewSpeed=0;
        Vec3 position,viewStart,viewEnd;float fovStart=50,fovEnd=50,time=0;};
    struct Route {std::vector<Vec3> points;std::vector<float> speeds;};
    struct Racer {std::unique_ptr<Model> model;std::unique_ptr<DriverAnimation> animation;int clip=-1,face=-1;Vec3 position;};
    // A podium racer's kart on the winning run.
    struct Kart {
        const GarageChoice* choice=nullptr;
        std::unique_ptr<Model> body,driver;std::array<std::unique_ptr<Model>,4> wheels;
        std::unique_ptr<DriverAnimation> animation;int clip=-1;
        std::vector<Vec3> lane;std::vector<float> along; // lane points and their distance from the start
        std::array<Vec3,4> wheelAt{};float radius=20,lift=0,speed=0;
    };
    struct Text {Texture texture;float width=0,height=0;};
    struct Confetti {float x,y,vx,vy,phase,size;uint32_t color;};
    bool loadKarts();
    void beginDriveCamera(int index);
    void stepDriveCamera();
    float kartDistance(unsigned kart) const;
    Vec3 kartPosition(unsigned kart,float distance) const;
    void drawKarts(Renderer& renderer);
    void drawOverlay(Renderer& renderer,unsigned settled,bool standings);
    Setup setup_;
    std::vector<std::unique_ptr<Model>> stage_,sky_;
    std::unique_ptr<Model> stand_,shadow_,trophy_;
    std::array<Racer,6> racers_;
    std::array<Kart,3> karts_;
    std::vector<Camera> cameras_;
    Text headline_,placeText_,classText_,cupName_,rankLabel_,gradeText_,teamText_,modeText_;
    Texture cupIcon_,confettiTexture_;
    std::vector<Confetti> confetti_;
    std::vector<DriveCamera> driveCameras_;std::vector<Route> routes_;
    unsigned driveFirst_=0;int driveCamera_=-1;
    float driveElapsed_=0,driveFov_=50,routeDistance_=0;Vec3 driveEye_,driveTarget_;
    unsigned shot_=0,shotTick_=0,tick_=0,ending_=0,driveTicks_=0,podiumTicks_=0,standingsTicks_=0;
    bool skipping_=false,driving_=false,standings_=false;
    // The podium's last view (the standings look up from it).
    Vec3 lastEye_,lastTarget_;
};
}
