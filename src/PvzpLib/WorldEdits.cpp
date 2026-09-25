#include "WorldEdits.h"
#include "../LawnApp.h"
#include "../Lawn/Board.h"
#include "../Lawn/Projectile.h"
#include "../Lawn/Coin.h"
namespace PvzpWorldEdits {
bool ClearProjectiles() noexcept { try { if(!gLawnApp||!gLawnApp->mBoard)return false; for(auto* projectile:gLawnApp->mBoard->mProjectiles)if(!projectile->mDead)projectile->Die();return true;}catch(...){return false;} }
bool ClearItems() noexcept { try { if(!gLawnApp||!gLawnApp->mBoard)return false; for(auto* coin:gLawnApp->mBoard->mCoins)if(!coin->mDead)coin->Die();return true;}catch(...){return false;} }
}
