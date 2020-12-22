/* $Id$ */

/** @file factory.hpp Factory to 'query' all available blitters. */

#ifndef BLITTER_FACTORY_HPP
#define BLITTER_FACTORY_HPP

#include "base.hpp"
#include "../debug.h"
#include "../string_func.h"
#include <map>

#if defined(WITH_COCOA)
bool QZ_CanDisplay8bpp();
#endif /* defined(WITH_COCOA) */

/**
 * The base factory, keeping track of all blitters.
 */
class BlitterFactoryBase {
private:
	const char *name; ///< The name of the blitter factory.

	struct StringCompare {
		bool operator () (const char *a, const char *b) const
		{
			return strcmp(a, b) < 0;
		}
	};

	typedef std::map<const char *, BlitterFactoryBase *, StringCompare> Blitters; ///< Map of blitter factories.

	/**
	 * Get the map with currently known blitters.
	 * @return The known blitters.
	 */
	static Blitters &GetBlitters()
	{
		static Blitters &s_blitters = *new Blitters();
		return s_blitters;
	}

	/**
	 * Get the currently active blitter.
	 * @return The currently active blitter.
	 */
	static Blitter **GetActiveBlitter()
	{
		static Blitter *s_blitter = NULL;
		return &s_blitter;
	}

protected:
	/**
	 * Register a blitter internally, based on his name.
	 * @param name the name of the blitter.
	 * @note an assert() will be trigger if 2 blitters with the same name try to register.
	 */
	void RegisterBlitter(const char *name)
	{
		/* Don't register nameless Blitters */
		if (name == NULL) return;

		this->name = strdup(name);
#if !defined(NDEBUG) || defined(WITH_ASSERT)
		/* NDEBUG disables asserts and gives a warning: unused variable 'P' */
		std::pair<Blitters::iterator, bool> P =
#endif /* !NDEBUG */
		GetBlitters().insert(Blitters::value_type(name, this));
		assert(P.second);
	}

public:
	BlitterFactoryBase() :
		name(NULL)
	{}

	virtual ~BlitterFactoryBase()
	{
		if (this->name == NULL) return;
		GetBlitters().erase(this->name);
		if (GetBlitters().empty()) delete &GetBlitters();
		free((void *)this->name);
	}

	/**
	 * Find the requested blitter and return his class.
	 * @param name the blitter to select.
	 * @post Sets the blitter so GetCurrentBlitter() returns it too.
	 */
	static Blitter *SelectBlitter(const char *name)
	{
#if defined(DEDICATED)
		const char *default_blitter = "null";
#else
		const char *default_blitter = "8bpp-optimized";

#if defined(WITH_COCOA)
		/* Some people reported lack of fullscreen support in 8 bpp mode.
		 * While we prefer 8 bpp since it's faster, we will still have to test for support. */
		if (!QZ_CanDisplay8bpp()) {
			/* The main display can't go to 8 bpp fullscreen mode.
			 * We will have to switch to 32 bpp by default. */
			default_blitter = "32bpp-anim";
		}
#endif /* defined(WITH_COCOA) */
#endif /* defined(DEDICATED) */
		if (GetBlitters().size() == 0) return NULL;
		const char *bname = (StrEmpty(name)) ? default_blitter : name;

		Blitters::iterator it = GetBlitters().begin();
		for (; it != GetBlitters().end(); it++) {
			BlitterFactoryBase *b = (*it).second;
			if (strcasecmp(bname, b->name) == 0) {
				Blitter *newb = b->CreateInstance();
				delete *GetActiveBlitter();
				*GetActiveBlitter() = newb;

				DEBUG(driver, 1, "Successfully %s blitter '%s'",StrEmpty(name) ? "probed" : "loaded", bname);
				return newb;
			}
		}
		return NULL;
	}

	/**
	 * Get the current active blitter (always set by calling SelectBlitter).
	 */
	static Blitter *GetCurrentBlitter()
	{
		return *GetActiveBlitter();
	}

	/**
	 * Fill a buffer with information about the blitters.
	 * @param p The buffer to fill.
	 * @param last The last element of the buffer.
	 * @return p The location till where we filled the buffer.
	 */
	static char *GetBlittersInfo(char *p, const char *last)
	{
		p += seprintf(p, last, "List of blitters:\n");
		Blitters::iterator it = GetBlitters().begin();
		for (; it != GetBlitters().end(); it++) {
			BlitterFactoryBase *b = (*it).second;
			p += seprintf(p, last, "%18s: %s\n", b->name, b->GetDescription());
		}
		p += seprintf(p, last, "\n");

		return p;
	}

	/**
	 * Get a nice description of the blitter-class.
	 */
	virtual const char *GetDescription() = 0;

	/**
	 * Create an instance of this Blitter-class.
	 */
	virtual Blitter *CreateInstance() = 0;
};

/**
 * A template factory, so ->GetName() works correctly. This because else some compiler will complain.
 */
template <class T>
class BlitterFactory: public BlitterFactoryBase {
public:
	BlitterFactory() { this->RegisterBlitter(((T *)this)->GetName()); }

	/**
	 * Get the long, human readable, name for the Blitter-class.
	 */
	const char *GetName();
};

extern char *_ini_blitter;

#endif /* BLITTER_FACTORY_HPP */
