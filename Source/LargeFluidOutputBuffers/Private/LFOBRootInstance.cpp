#include "LFOBRootInstance.h"

void ULFOBRootInstance::ProcessOutputBuffers(AFGBuildableManufacturer* manufacturer)
{
    // Is manufacturer valid?  
    if (not IsValid(manufacturer))
    {
        UE_LOG(LogLFOB, Error, TEXT("AFGBuildableManufacturer IsValid Failed."));
        return;
    }

	ProcessOutputBuffersInternal(manufacturer, manufacturer->GetCurrentRecipe(), manufacturer->GetCurrentProductionBoost());
}

void ULFOBRootInstance::ProcessOutputBuffers(AFGBuildableManufacturer* manufacturer, TSubclassOf<class UFGRecipe> recipe)
{
    // Is manufacturer valid?  
    if (not IsValid(manufacturer))
    {
        UE_LOG(LogLFOB, Error, TEXT("AFGBuildableManufacturer IsValid Failed."));
        return;
    }

	ProcessOutputBuffersInternal(manufacturer, recipe, manufacturer->GetCurrentProductionBoost());
}

void ULFOBRootInstance::ProcessOutputBuffers(AFGBuildableManufacturer* manufacturer, float productionBoost)
{
    // Is manufacturer valid?  
    if (not IsValid(manufacturer))
    {
        UE_LOG(LogLFOB, Error, TEXT("AFGBuildableManufacturer IsValid Failed."));
        return;
    }

	ProcessOutputBuffersInternal(manufacturer, manufacturer->GetCurrentRecipe(), productionBoost);
}

void ULFOBRootInstance::ProcessOutputBuffers(AFGBuildableManufacturer* manufacturer, TSubclassOf< class UFGRecipe > recipe, float productionBoost)
{
	// Is manufacturer valid?  
	if (not IsValid(manufacturer))
	{
		UE_LOG(LogLFOB, Error, TEXT("AFGBuildableManufacturer IsValid Failed."));
		return;
	}

	ProcessOutputBuffersInternal(manufacturer, recipe, productionBoost);
}

void ULFOBRootInstance::ProcessInputBuffers(AFGBuildableManufacturer* manufacturer)
{
	// Is manufacturer valid?  
	if (not IsValid(manufacturer))
	{
		UE_LOG(LogLFOB, Error, TEXT("AFGBuildableManufacturer IsValid Failed."));
		return;
	}

	ProcessInputBuffersInternal(manufacturer, manufacturer->GetCurrentRecipe());
}

void ULFOBRootInstance::ProcessInputBuffers(AFGBuildableManufacturer* manufacturer, TSubclassOf<class UFGRecipe> recipe)
{
	// Is manufacturer valid?  
	if (not IsValid(manufacturer))
	{
		UE_LOG(LogLFOB, Error, TEXT("AFGBuildableManufacturer IsValid Failed."));
		return;
	}

	ProcessInputBuffersInternal(manufacturer, recipe);
}

void ULFOBRootInstance::ProcessOutputBuffersInternal(AFGBuildableManufacturer* manufacturer, TSubclassOf< class UFGRecipe > recipe, float productionBoost)
{
	// Is recipe set?
	if (not IsValid(recipe))
	{
		UE_LOG(LogLFOB, Display, TEXT("%s Recipe not set, nothing to do."), *manufacturer->GetName());
		return;
	}

	// Grab the output inventory
	UFGInventoryComponent* inventory = manufacturer->GetOutputInventory();
	// Do we have a valid output inventory?  This should be impossible for a well defined machine, but let's be safe.
	if (not IsValid(inventory))
	{
		UE_LOG(LogLFOB, Error, TEXT("%s output inventory IsValid Failed"), *manufacturer->GetName());
		return;
	}

	// Grab the Configuration for this mod.
	FLargeFluidOutputBuffersConfigurationStruct config = FLargeFluidOutputBuffersConfigurationStruct::GetActiveConfig(GetWorld());

	ProcessingParameters parameters;
	// Should we auto calculate the correct buffer sizes?
	parameters.autoSetBuffers = config.DynamicSettings.AutoSetBuffers;
	// Can we exceed 600m3 during an autoset?
	parameters.exceedMax = config.DynamicSettings.ExceedPipeMax;
	parameters.fixedBufferSize = config.OutputBufferSizeFluids;
	parameters.productionBoost = productionBoost;

	ProcessInventory(inventory, parameters, recipe);

	// Now we have done all this we should recalculate if this machine can produce
	// Fixes a bug where depending on order execution a machine can be left in an infinite idle state post buffer
	// modification
	RecaculateCanProduceOutput(manufacturer);
}

void ULFOBRootInstance::ProcessInputBuffersInternal(AFGBuildableManufacturer* manufacturer, TSubclassOf<class UFGRecipe> recipe)
{
	// Grab the Configuration for this mod.
	FLargeFluidOutputBuffersConfigurationStruct config = FLargeFluidOutputBuffersConfigurationStruct::GetActiveConfig(GetWorld());

	// Should we even be doing this?
	if (!config.EnableInputAdjustments)
		return;

	// Is recipe set?
	if (not IsValid(recipe))
	{
		UE_LOG(LogLFOB, Display, TEXT("%s Recipe not set, nothing to do."), *manufacturer->GetName());
		return;
	}

	// Grab the input inventory
	UFGInventoryComponent* inventory = manufacturer->GetInputInventory();
	// Do we have a valid input inventory?  This should be impossible for a well defined machine, but let's be safe.
	if (not IsValid(inventory))
	{
		UE_LOG(LogLFOB, Error, TEXT("%s input inventory IsValid Failed"), *manufacturer->GetName());
		return;
	}

	ProcessingParameters parameters;
	// Should we auto calculate the correct buffer sizes?
	parameters.autoSetBuffers = config.InputDynamicSettings.AutoSetBuffers;
	// Can we exceed 600m3 during an autoset?
	parameters.exceedMax = config.InputDynamicSettings.ExceedPipeMax;
	parameters.fixedBufferSize = config.InputBufferSizeFluids;
	parameters.direction = Direction::INPUT;

	ProcessInventory(inventory, parameters, recipe);
}

/*
* Process the provided inventory against the provided items, this could be an input or output inventory compared against ingredients or products.
*/
void ULFOBRootInstance::ProcessInventory(UFGInventoryComponent* inventory, ProcessingParameters& parameters, TSubclassOf<class UFGRecipe> recipe)
{
	FString mode = parameters.autoSetBuffers ? parameters.exceedMax ? TEXT("DYNAMIC+") : TEXT("DYNAMIC") : TEXT("FIXED");
	FString tDirection = parameters.direction == Direction::OUTPUT ? TEXT("Output") : TEXT("Input");
	TArray<FItemAmount> items = parameters.direction == Direction::OUTPUT ? UFGRecipe::GetProducts(recipe) : UFGRecipe::GetIngredients(recipe);

	// Check those recipe items for being a gas or liquid and set the buffer size if they are
	for (int32 i = 0; i < items.Num(); i++)
	{
		TSubclassOf<class UFGItemDescriptor> itemClass = items[i].ItemClass;
		if (IsValid(itemClass))
		{
			EResourceForm form = UFGItemDescriptor::GetForm(itemClass);
			FString itemDesc = UFGItemDescriptor::GetItemName(itemClass).ToString();

			if (form == EResourceForm::RF_GAS || form == EResourceForm::RF_LIQUID)
			{
				// Are we supposed to automatically set the buffer size?  If so calculate the correct value.
				if (parameters.autoSetBuffers)
					ProcessDynamicBufferSize(items[i], parameters);
				else
					ProcessFixedBufferSize(parameters);

				inventory->AddArbitrarySlotSize(i, parameters.sizeInLitres);
				UE_LOG(LogLFOB, Display, TEXT("[MODE = %s] Found %s %s '%s' at index %d, setting buffer to %d m3"), *mode, form == EResourceForm::RF_GAS ? TEXT("Gas") : TEXT("Fluid"), *tDirection, *itemDesc, i, parameters.sizeInCubicMetres);
			}
			else if (form == EResourceForm::RF_SOLID)
			{
				UE_LOG(LogLFOB, Display, TEXT("[MODE = %s] Found Solid %s '%s' at index %d, Doing nothing for now."), *mode, *tDirection, *itemDesc, i, parameters.sizeInCubicMetres);
			}
		}
	}
}

/*
* Determine the calculated buffer size for an inventory buffer in litres.
*/
void ULFOBRootInstance::ProcessFixedBufferSize(ProcessingParameters& parameters)
{
	// Grab the requested size in m3
	parameters.sizeInCubicMetres = parameters.fixedBufferSize;
	// Make sure we are never less than 50m3 and never more than 600m3, just a basic sanity check.
	if (parameters.sizeInCubicMetres < 50)
	{
		parameters.sizeInCubicMetres = 50;
	}
	else if (parameters.sizeInCubicMetres > 600)
	{
		parameters.sizeInCubicMetres = 600;
	}
	// Target Size for buffer in litres.    
	parameters.sizeInLitres = 1000 * parameters.sizeInCubicMetres;
}

void ULFOBRootInstance::ProcessDynamicBufferSize(FItemAmount item, ProcessingParameters& parameters)
{
	parameters.sizeInLitres = ceil(item.Amount * 2 * parameters.productionBoost);
	// Ensure we are never less than 50m3
	if (parameters.sizeInLitres < 50000)
	{
		parameters.sizeInLitres = 50000;
	}
	// Only do this check if we are not exceeding 600m3
	else if (!parameters.exceedMax && parameters.sizeInLitres > 600000)
	{
		parameters.sizeInLitres = 600000;
	}
	parameters.sizeInCubicMetres = parameters.sizeInLitres / 1000;
}

void ULFOBRootInstance::RecaculateCanProduceOutput(AFGBuildableManufacturer* manufacturer)
{
	manufacturer->bCachedHasOutputSpace = 1;
	UFGInventoryComponent* mOutputInventory = manufacturer->mOutputInventory;
	if (mOutputInventory)
	{
		const UFGRecipe* mCachedRecipe = manufacturer->mCachedRecipe;
		if (mCachedRecipe)
		{
			// Go through the recipe and determine if we can actually store a boosted output
			for (FItemAmount fItemAmount : mCachedRecipe->GetProducts())
			{
				TSubclassOf<class UFGItemDescriptor> itemClass = fItemAmount.ItemClass;
				FInventoryStack iStack = FInventoryStack(fItemAmount.Amount * manufacturer->mCurrentProductionBoost, itemClass);
				manufacturer->bCachedHasOutputSpace &= mOutputInventory->HasEnoughSpaceForStack(iStack);
			}
		}
	}
}

void ULFOBRootInstance::DispatchLifecycleEvent(ELifecyclePhase phase)
{
	Super::DispatchLifecycleEvent(phase);
	if (phase == ELifecyclePhase::POST_INITIALIZATION)
    {
        if (!WITH_EDITOR) 
        {
			/*
			* Bind to the Set Recpie method, this is called when the user selects a recipe.
			* 
			* At this point we need to analyse that recipe and determine what products are fluids 
			* and adjust the buffer to accomodate either 2 cycles or set the value to the user define buffer
			* value.
			*/
            SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableManufacturer, AFGBuildableManufacturer::SetRecipe,
                [&](AFGBuildableManufacturer* self, TSubclassOf< class UFGRecipe > recipe)
                {
                    UE_LOG(LogLFOB, Display, TEXT("SetRecipe Called on %s, Checking Output Buffers."), *self->GetName());
                    ProcessOutputBuffers(self, recipe);
					ProcessInputBuffers(self, recipe);
                });
			/*
			* Bind to the BeginPlay method, this is called when a machine is instantiated in the world.	
			* 
			* This could be a new machine in the world (in this case it would not have a recipe so nothing would be done) 
			* but more importantly it is called for each machine when a save game is loaded.
			* Any modifications to an output inventory are not stored in the savegame so we need to go through every machine
			* in the world and set its correct buffer value, either calculated or preset by the user.
			*/
            SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableManufacturer, AFGBuildableManufacturer::BeginPlay,
                [&](AFGBuildableManufacturer* self)
                {
                    UE_LOG(LogLFOB, Display, TEXT("BeginPlay Called on %s, Checking Output Buffers."), *self->GetName());
                    ProcessOutputBuffers(self);
					ProcessInputBuffers(self);
                });
			/*
			* Bind to the SetCurrentProductionBoost, this is called whenever sommersloops are added or removed and the next cycle has started.
			* 
			* If the production boost has changed we need to recalculate the buffer size on this machine or set to the users defined value
			* as boosting will increase the cycles production amount, technically in user defined fixed buffer mode this doesn't need to change anything
			* as it would have been set on BeginPlay or SetRecipe.
			*/
            SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGBuildableManufacturer, AFGBuildableManufacturer::SetCurrentProductionBoost,
                [&](AFGBuildableManufacturer* self, float newProductionBoost)
                {
                    UE_LOG(LogLFOB, Display, TEXT("SetCurrentProductionBoost Called on %s, Checking Output Buffers."), *self->GetName());
                    ProcessOutputBuffers(self, newProductionBoost);
					ProcessInputBuffers(self);
                });
        }
	}
}