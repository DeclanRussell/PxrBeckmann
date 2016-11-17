/*  $Date: 2015/07/06 $  $Revision: #2 $
# ------------------------------------------------------------------------------
#
# Copyright (c) 2014 Pixar Animation Studios. All rights reserved.
#
# The information in this file (the "Software") is provided for the
# exclusive use of the software licensees of Pixar.  Licensees have
# the right to incorporate the Software into other products for use
# by other authorized software licensees of Pixar, without fee.
# Except as expressly permitted herein, the Software may not be
# disclosed to third parties, copied or duplicated in any form, in
# whole or in part, without the prior written permission of
# Pixar Animation Studios.
#
# The copyright notices in the Software and this entire statement,
# including the above license grant, this restriction and the
# following disclaimer, must be included in all copies of the
# Software, in whole or in part, and all permitted derivative works of
# the Software, unless such copies or derivative works are solely
# in the form of machine-executable object code generated by a
# source language processor.
#
# PIXAR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
# ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT
# SHALL PIXAR BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES
# OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
# WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
# ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
# SOFTWARE.
#
# Pixar
# 1200 Park Ave
# Emeryville CA 94608
#
# ------------------------------------------------------------------------------
*/
#include "RixBxdf.h"
#ifdef RENDERMAN21
    #include "RixRNG.h"
#endif
#include "RixShadingUtils.h"
#include <cstring> // memset

static const RtFloat k_minfacing = .0001f; // NdV < k_minfacing is invalid

static const unsigned char k_reflBlinnLobeId = 0;

static RixBXLobeSampled s_reflBlinnLobe;

static RixBXLobeTraits s_reflBlinnLobeTraits;

class PxrBeckmann : public RixBsdf
{
public:

    PxrBeckmann(RixShadingContext const *sc, RixBxdfFactory *bx,
               RixBXLobeTraits const &lobesWanted,
               RtColorRGB const *color, RtFloat const *width) :
        RixBsdf(sc, bx),
        m_lobesWanted(lobesWanted),
        m_color(color),
        m_width(width)
    {
        RixBXLobeTraits lobes = s_reflBlinnLobeTraits;

        m_lobesWanted &= lobes;

        sc->GetBuiltinVar(RixShadingContext::k_P, &m_P);
        sc->GetBuiltinVar(RixShadingContext::k_Nn, &m_Nn);
        sc->GetBuiltinVar(RixShadingContext::k_Ngn, &m_Ngn);
        sc->GetBuiltinVar(RixShadingContext::k_Tn, &m_Tn);
        sc->GetBuiltinVar(RixShadingContext::k_Vn, &m_Vn);

    }

    virtual RixBXEvaluateDomain GetEvaluateDomain()
    {
#ifdef RENDERMAN21
        return k_RixBXReflect;   // Same as v19/20 kRixBXFront
#else
        return k_RixBXFront;  // two-sided, but opaque
#endif
    }
    virtual void GetAggregateLobeTraits(RixBXLobeTraits *t)
    {
        *t = m_lobesWanted;
    }

#ifdef RENDERMAN21
    virtual void GenerateSample(RixBXTransportTrait transportTrait,
                                RixBXLobeTraits const *lobesWanted,
                                RixRNG *rng,
                                RixBXLobeSampled *lobeSampled,
                                RtVector3   *Ln,
                                RixBXLobeWeights &W,
                                RtFloat *FPdf, RtFloat *RPdf,
                                RtColorRGB* compTrans)
#else
    virtual void GenerateSample(RixBXTransportTrait transportTrait,
                                RixBXLobeTraits const *lobesWanted,
                                RixRNG *rng,
                                RixBXLobeSampled *lobeSampled,
                                RtVector3   *Ln,
                                RixBXLobeWeights &W,
                                RtFloat *FPdf, RtFloat *RPdf)
#endif
    {
        RtInt nPts = shadingCtx->numPts;
        RixBXLobeTraits all = GetAllLobeTraits();
        RtFloat2 *xi = (RtFloat2 *) RixAlloca(sizeof(RtFloat2) * nPts);
        rng->DrawSamples2D(nPts,xi);

        RtColorRGB *reflDiffuseWgt = NULL;

        RtNormal3 Nf;

        for(int i = 0; i < nPts; i++)
        {
            lobeSampled[i].SetValid(false);

            RixBXLobeTraits lobes = (all & lobesWanted[i]);
            bool doDiff = (lobes & s_reflBlinnLobeTraits).HasAny();

            if (!reflDiffuseWgt && doDiff)
                reflDiffuseWgt = W.AddActiveLobe(s_reflBlinnLobe);
            if (doDiff)
            {
                // we generate samples on the (front) side of Vn since
                // we have no translucence effects.
                RtFloat NdV;
                NdV = m_Nn[i].Dot(m_Vn[i]);
                if(NdV >= 0.f)
                {
                    Nf = m_Nn[i];
                }
                else
                {
                    Nf = -m_Nn[i];
                    NdV = -NdV;
                }
                if(NdV > k_minfacing)
                {
                    generate(NdV, Nf, m_Tn[i], m_color[i],m_width[i], xi[i],
                             Ln[i],m_Vn[i], reflDiffuseWgt[i], FPdf[i], RPdf[i]);
                    lobeSampled[i] = s_reflBlinnLobe;
                }
                // else invalid.. NullTrait
            }
        }

    }

#ifdef RENDERMAN21
    virtual void EvaluateSample(RixBXTransportTrait transportTrait,
                                RixBXLobeTraits const *lobesWanted,
                                RixRNG *rng,
                                RixBXLobeTraits *lobesEvaluated,
                                RtVector3 const *Ln, RixBXLobeWeights &W,
                                RtFloat *FPdf, RtFloat *RPdf)
#else
    virtual void EvaluateSample(RixBXTransportTrait transportTrait,
                                RixBXLobeTraits const *lobesWanted,
                                RixBXLobeTraits *lobesEvaluated,
                                RtVector3 const *Ln, RixBXLobeWeights &W,
                                RtFloat *FPdf, RtFloat *RPdf)
#endif
    {
        RtNormal3 Nf;
        RtInt nPts = shadingCtx->numPts;
        RixBXLobeTraits all = GetAllLobeTraits();

        RtColorRGB *reflDiffuseWgt = NULL;

        for(int i = 0; i < nPts; i++)
        {
            lobesEvaluated[i].SetNone();
            RixBXLobeTraits lobes = (all & lobesWanted[i]);
            bool doDiff = (lobes & s_reflBlinnLobeTraits).HasAny();

            if (!reflDiffuseWgt && doDiff)
                reflDiffuseWgt = W.AddActiveLobe(s_reflBlinnLobe);

            if (doDiff)
            {
                RtFloat NdV;
                NdV = m_Nn[i].Dot(m_Vn[i]);
                if(NdV >= 0.f)
                    Nf = m_Nn[i];
                else
                {
                    Nf = -m_Nn[i];
                    NdV = -NdV;
                }
                if(NdV > k_minfacing)
                {
                    RtFloat NdL = Nf.Dot(Ln[i]);
                    if(NdL > 0.f)
                    {
                        evaluate(NdV, NdL,Nf, m_color[i],m_width[i],Ln[i],m_Vn[i],
                                 reflDiffuseWgt[i], FPdf[i], RPdf[i]);
                        lobesEvaluated[i] |= s_reflBlinnLobeTraits;
                    }
                }
            }
        }


    }

#ifdef RENDERMAN21
    virtual void EvaluateSamplesAtIndex(RixBXTransportTrait transportTrait,
                                            RixBXLobeTraits const &lobesWanted,
                                            RixRNG *rng,
                                            RtInt index, RtInt nsamps,
                                            RixBXLobeTraits *lobesEvaluated,
                                            RtVector3 const *Ln,
                                            RixBXLobeWeights &W,
                                            RtFloat *FPdf, RtFloat *RPdf)
#else
    virtual void EvaluateSamplesAtIndex(RixBXTransportTrait transportTrait,
                                        RixBXLobeTraits const &lobesWanted,
                                        RtInt index, RtInt nsamps,
                                        RixBXLobeTraits *lobesEvaluated,
                                        RtVector3 const *Ln,
                                        RixBXLobeWeights &W,
                                        RtFloat *FPdf, RtFloat *RPdf)
#endif
    {
        for (int i = 0; i < nsamps; i++)
            lobesEvaluated[i].SetNone();

        RixBXLobeTraits lobes = lobesWanted & GetAllLobeTraits();
        bool doDiff = (lobes & s_reflBlinnLobeTraits).HasAny();

        if(!doDiff)
            return;

        RtNormal3 const &Nn = m_Nn[index];
        RtNormal3 const &Ngn = m_Ngn[index];
        RtVector3 const &Vn = m_Vn[index];
        RtColorRGB const &color = m_color[index];
        RtFloat const &width = m_width[index];


        // Make any lobes that we may evaluate or write to active lobes,
        // initialize their lobe weights to zero and fetch a pointer to the
        // lobe weight arrays.

        RtColorRGB *reflDiffuseWgt = doDiff
            ? W.AddActiveLobe(s_reflBlinnLobe) : NULL;

        RtNormal3 Nf;
        RtFloat NdV;

        NdV = Nn.Dot(Vn);
        if(NdV >= .0f)
            Nf = Nn;
        else
        {
            Nf = -Nn;
            NdV = -NdV;
        }
        RtFloat NfdV;
        NfdV = NdV;
        if(NdV > k_minfacing)
        {
            for(int i=0; i<nsamps; ++i)
            {
                RtFloat NdL = Nf.Dot(Ln[i]);
                if(NdL > 0.f)
                {
                    evaluate(NfdV, NdL,Nf, color,width,Ln[index],Vn,
                             reflDiffuseWgt[i], FPdf[i], RPdf[i]);
                    lobesEvaluated[i] |= s_reflBlinnLobeTraits;
                }
            }
        }
    }


private:


    PRMAN_INLINE
    void generate(RtFloat NdV,
                 const RtNormal3 &Nn, const RtVector3 &Tn,
                 const RtColorRGB &color,
                 const RtFloat &width,
                 const RtFloat2 &xi,
                 RtVector3 &Ln, RtVector3 const &inDir, RtColorRGB  &W,
                 RtFloat &FPdf, RtFloat &RPdf)
    {
        RtVector3 TX, TY;
        RixComputeShadingBasis(Nn, Tn, TX, TY);
        // Compute our new direction
        // half angle
        float cosTheta = sqrtf(1.f/(1.f-(width*width*logf(1.f-xi.x))));
//        float cosTheta = cos(atan(sqrtf(-(width*width*log(1.f-xi.x)))));
        float cosThetaSqrd = cosTheta*cosTheta;
        float sinTheta = sqrtf(fmax(0.f,1.f-cosThetaSqrd));
        float tanSqrd = (sinTheta*sinTheta)/cosThetaSqrd;
        float phi = xi.y * 2.f * M_PI;
        float x = sinTheta*cosf(phi);
        float y = sinTheta*sinf(phi);
        float z = cosTheta;
        RtVector3 m = x * TX + y * TY + z * Nn;
        // scattered light direction
        Ln = 2.f*inDir.AbsDot(m)*m-inDir;

        RtVector3 wh = (Ln+inDir);
        wh.Normalize();

        //Beckmann NDF
        float D;
        if(cosTheta<=0.f)
            D = 0.f;
        else
        {
//            D = (1.f/(M_PI*width*width*cosThetaSqrd*cosThetaSqrd))*expf(-tanSqrd/(width*width));
            D = (1.f/(M_PI*width*width*cosThetaSqrd*cosThetaSqrd))*expf((cosThetaSqrd-1.f)/(width*width*cosThetaSqrd));

        }
        // Shadow function
        float IdN = Nn.Dot(inDir);
        float OdN = Nn.Dot(Ln);
        float G1,G2;
        if(IdN<=0.f)
            G1 = 0.f;
        else
        {
            float sinVSqrd = 1.f-IdN*IdN;
            float tanV = sqrtf(sinVSqrd/(IdN*IdN));
            float a = 1.f/(width*tanV);
            if(a<1.6f)
            {
                G1 = (3.535f*a+2.181f*a*a)/(1.f+2.276f*a+2.577f*a*a);
            }
            else
            {
                G1 = 1.f;
            }
        }
        if(OdN<=0.f)
            G2 = 0.f;
        else
        {
            float sinVSqrd = 1.f-OdN*OdN;
            float tanV = sqrtf(sinVSqrd/(OdN*OdN));
            float a = 1.f/(width*tanV);
            if(a<1.6f)
            {
                G2 = (3.535f*a+2.181f*a*a)/(1.f+2.276f*a+2.577f*a*a);
            }
            else
            {
                G2 = 1.f;
            }
        }
        //Blinn weight
        float radiance = (G1*G2*D)/(4.f*IdN);
        W = color * radiance;
        FPdf = D * G1 / (4.f * IdN);
        RPdf = D * G2 / (4.f * OdN);

    }

    PRMAN_INLINE
    void evaluate(RtFloat NdV, RtFloat NdL, RtNormal3 &Nn, const RtColorRGB &color, const RtFloat &width,
                 RtVector3 Ln, RtVector3 const &inDir,
                 RtColorRGB  &W, RtFloat &FPdf, RtFloat &RPdf)
    {
        //Compute our blinn weighting and PDF
        //PDF
        RtVector3 m = Ln + inDir;
        m.Normalize();
        float cosTheta = fabs(m.Dot(Nn));
        float cosThetaSqrd = cosTheta*cosTheta;
        float sinTheta = sqrtf(fmax(0.f,1.f-cosThetaSqrd));
        float tanSqrd = (sinTheta*sinTheta)/cosThetaSqrd;

        //Beckmann NDF
        float D;
        if(cosTheta<=0.f)
            D = 0.f;
        else
            D = (1.f/(M_PI*width*width*cosThetaSqrd*cosThetaSqrd))*expf(-tanSqrd/(width*width));

        // Shadow function
        float IdN = Nn.Dot(inDir);
        float OdN = Nn.Dot(Ln);
        float G1,G2;
        if(IdN<=0.f)
            G1 = 0.f;
        else
        {
            float sinVSqrd = 1.f-IdN*IdN;
            float tanV = sqrtf(sinVSqrd/(IdN*IdN));
            float a = 1.f/(width*tanV);
            if(a<1.6f)
            {
                G1 = (3.535f*a+2.181f*a*a)/(1.f+2.276f*a+2.577f*a*a);
            }
            else
            {
                G1 = 1.f;
            }
        }
        if(OdN<=0.f)
            G2 = 0.f;
        else
        {
            float sinVSqrd = 1.f-OdN*OdN;
            float tanV = sqrtf(sinVSqrd/(OdN*OdN));
            float a = 1.f/(width*tanV);
            if(a<1.6f)
            {
                G2 = (3.535f*a+2.181f*a*a)/(1.f+2.276f*a+2.577f*a*a);
            }
            else
            {
                G2 = 1.f;
            }
        }
        //Blinn weight
        float radiance = (G1*G2*D)/(4.f*IdN);
        W = color * radiance;
        FPdf = D * G1 / (4.f * IdN);
        RPdf = D * G2 / (4.f * OdN);
    }
private:
    RixBXLobeTraits m_lobesWanted;
    RtColorRGB const *m_color;
    RtFloat const *m_width;
    RtPoint3 const* m_P;
    RtVector3 const* m_Vn;
    RtVector3 const* m_Tn;
    RtNormal3 const* m_Nn;
    RtNormal3 const* m_Ngn;
};

// PxrBeckmannFactory Implementation
class PxrBeckmannFactory : public RixBxdfFactory
{
public:


    PxrBeckmannFactory();
    ~PxrBeckmannFactory();

    virtual int Init(RixContext &, char const *pluginpath);
    RixSCParamInfo const *GetParamTable();
    virtual void Finalize(RixContext &);

    virtual void Synchronize(RixContext &ctx, RixSCSyncMsg syncMsg,
                             RixParameterList const *parameterList);

    virtual int CreateInstanceData(RixContext &,
                                   char const *handle,
                                   RixParameterList const *,
                                   InstanceData *id);

    virtual int GetInstanceHints(RtConstPointer instanceData) const;

    virtual RixBsdf *BeginScatter(RixShadingContext const *,
                                  RixBXLobeTraits const &lobesWanted,
                                  RixSCShadingMode sm,
                                  RtConstPointer instanceData);
    virtual void EndScatter(RixBsdf *);

  private:
    // these hold the default (def) values
    //----------------------------------------------------------------------------------------------------------------------
    /// @brief Defualt colour of our beckmann BRDF
    //----------------------------------------------------------------------------------------------------------------------
    RtColorRGB m_colorDflt;
    //----------------------------------------------------------------------------------------------------------------------
    /// @brief Defualt width of our beckmann BRDF
    //----------------------------------------------------------------------------------------------------------------------
    RtFloat m_widthDflt;
    //----------------------------------------------------------------------------------------------------------------------

};

extern "C" PRMANEXPORT RixBxdfFactory *CreateRixBxdfFactory(const char *hint)
{
    return new PxrBeckmannFactory();
}

extern "C" PRMANEXPORT void DestroyRixBxdfFactory(RixBxdfFactory *bxdf)
{
    delete (PxrBeckmannFactory *) bxdf;
}

/*-----------------------------------------------------------------------*/
PxrBeckmannFactory::PxrBeckmannFactory()
{
    m_colorDflt = RtColorRGB(.5f);
    m_widthDflt = 1.f;
}

PxrBeckmannFactory::~PxrBeckmannFactory()
{
}

// Init
//  should be called once per RIB-instance. We look for parameter name
//  errors, and "cache" an understanding of our graph-evaluation requirements
//  in the form of allocation sizes.
int
PxrBeckmannFactory::Init(RixContext &ctx, char const *pluginpath)
{
    return 0;
}

// Synchronize: delivers occasional status information
// from the renderer. Parameterlist contents depend upon the SyncMsg.
// This method is optional and the default implementation ignores all
// events.
void
PxrBeckmannFactory::Synchronize(RixContext &ctx, RixSCSyncMsg syncMsg,
                              RixParameterList const *parameterList)
{
    if (syncMsg == k_RixSCRenderBegin)
    {
        s_reflBlinnLobe = RixBXLookupLobeByName(ctx, false, true, true, false,
                                                  k_reflBlinnLobeId,
                                                  "Specular");

        s_reflBlinnLobeTraits = RixBXLobeTraits(s_reflBlinnLobe);
     }
}

enum paramIds
{
    k_color,
    k_width,
    k_numParams
};

RixSCParamInfo const *
PxrBeckmannFactory::GetParamTable()
{
    // see .args file for comments, etc...
    static RixSCParamInfo s_ptable[] =
    {
        RixSCParamInfo("color", k_RixSCColor),
        RixSCParamInfo("width", k_RixSCFloat),
        RixSCParamInfo() // end of table
    };
    return &s_ptable[0];
}

// CreateInstanceData:
//    analyze plist to determine our response to GetOpacityHints.
//    Checks these inputs:
//          transmissionBehavior (value),
//          presence (networked)
int
PxrBeckmannFactory::CreateInstanceData(RixContext &ctx,
                                      char const *handle,
                                      RixParameterList const *plist,
                                      InstanceData *idata)
{
    RtUInt64 req = k_TriviallyOpaque;
    idata->data = (void *) req; // no memory allocated, overload pointer
    idata->freefunc = NULL;
    return 0;
}

int
PxrBeckmannFactory::GetInstanceHints(RtConstPointer instanceData) const
{
    // our instance data is the RixBxdfFactory::InstanceHints bitfield.
    InstanceHints const &hints = (InstanceHints const&) instanceData;
    return hints;
}

// Finalize:
//  companion to Init, called with the expectation that any data
//  allocated there will be released here.
void
PxrBeckmannFactory::Finalize(RixContext &)
{
}
RixBsdf *
PxrBeckmannFactory::BeginScatter(RixShadingContext const *sCtx,
                                RixBXLobeTraits const &lobesWanted,
                                RixSCShadingMode sm,
                                RtConstPointer instanceData)
{
    // Get all input data
    RtColorRGB const * color;
    RtFloat const * width;
    sCtx->EvalParam(k_color, -1, &color, &m_colorDflt, true);
    sCtx->EvalParam(k_width, -1, &width, &m_widthDflt, true);

    RixShadingContext::Allocator pool(sCtx);
    void *mem = pool.AllocForBxdf<PxrBeckmann>(1);

    // Must use placement new to set up the vtable properly
    PxrBeckmann *eval = new (mem) PxrBeckmann(sCtx, this, lobesWanted, color,width);

    return eval;
}

void
PxrBeckmannFactory::EndScatter(RixBsdf *)
{
}
