/*
 * Original work Copyright (c) 2006-2009 Erin Catto http://www.box2d.org
 * Modified work Copyright (c) 2017 Louis Langholtz https://github.com/louis-langholtz/PlayRho
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef ContactFeature_hpp
#define ContactFeature_hpp

#include <PlayRho/Common/Math.hpp>

namespace playrho
{
    /// Contact Feature.
    /// @details The features that intersect to form the contact point.
    /// @note This structure is designed to be compact and passed-by-value.
    /// @note This data structure is 4-bytes large.
    struct ContactFeature
    {
        using Index = std::uint8_t; ///< Index type.
        
        enum Type: std::uint8_t
        {
            e_vertex = 0,
            e_face = 1
        };
                
        // Fit data into 4-byte large structure...
        
        Type typeA; ///< The feature type on shape A
        Index indexA; ///< Feature index on shape A
        Type typeB; ///< The feature type on shape B
        Index indexB; ///< Feature index on shape B
    };
    
    constexpr ContactFeature GetVertexVertexContactFeature(ContactFeature::Index a, ContactFeature::Index b) noexcept
    {
        return ContactFeature{ContactFeature::e_vertex, a, ContactFeature::e_vertex, b};
    }

    constexpr ContactFeature GetVertexFaceContactFeature(ContactFeature::Index a, ContactFeature::Index b) noexcept
    {
        return ContactFeature{ContactFeature::e_vertex, a, ContactFeature::e_face, b};
    }
    
    constexpr ContactFeature GetFaceVertexContactFeature(ContactFeature::Index a, ContactFeature::Index b) noexcept
    {
        return ContactFeature{ContactFeature::e_face, a, ContactFeature::e_vertex, b};
    }
    
    constexpr ContactFeature GetFaceFaceContactFeature(ContactFeature::Index a, ContactFeature::Index b) noexcept
    {
        return ContactFeature{ContactFeature::e_face, a, ContactFeature::e_face, b};
    }
        
    /// Flips contact features information.
    constexpr ContactFeature Flip(ContactFeature val) noexcept
    {
        return ContactFeature{val.typeB, val.indexB, val.typeA, val.indexA};
    }
    
    constexpr bool operator==(ContactFeature lhs, ContactFeature rhs) noexcept
    {
        return (lhs.typeA == rhs.typeA) && (lhs.typeB == rhs.typeB) && (lhs.indexA == rhs.indexA) && (lhs.indexB == rhs.indexB);
    }

    constexpr bool operator!=(ContactFeature lhs, ContactFeature rhs) noexcept
    {
        return (lhs.typeA != rhs.typeA) || (lhs.typeB != rhs.typeB) || (lhs.indexA != rhs.indexA) || (lhs.indexB != rhs.indexB);
    }
    
}; // namespace playrho

#endif /* ContactFeature_hpp */
